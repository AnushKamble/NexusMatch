import asyncio
import json
import os
import sys
import subprocess
from pathlib import Path
from typing import Set, Optional
from contextlib import asynccontextmanager

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse, JSONResponse
from pydantic import BaseModel

ROOT_DIR = Path(__file__).resolve().parent.parent
STATIC_DIR = Path(__file__).resolve().parent / "static"

sys.path.insert(0, str(ROOT_DIR))

class EngineBridge:
    def __init__(self):
        self.process: Optional[asyncio.subprocess.Process] = None
        self.lock = asyncio.Lock()
        self.simulating = False
        self.sim_task: Optional[asyncio.Task] = None

    async def start(self):
        # Determine executable path and command
        if sys.platform == "win32":
            # WSL2 path conversion
            wsl_root = "/mnt/c/" + str(ROOT_DIR)[3:].replace("\\", "/")
            cmd = ["wsl", "-d", "Ubuntu-22.04", "--cd", wsl_root, "./build/nexusmatch_server"]
        else:
            server_bin = ROOT_DIR / "build" / "nexusmatch_server"
            if not server_bin.exists():
                raise RuntimeError(f"Engine binary not found at {server_bin}. Run cmake --build build first.")
            cmd = [str(server_bin)]

        print(f"[NexusMatch Bridge] Starting engine subprocess: {' '.join(cmd)}")
        self.process = await asyncio.create_subprocess_exec(
            *cmd,
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )
        print("[NexusMatch Bridge] C++ matching engine subprocess initialized.")

    async def send_command(self, payload: dict) -> dict:
        if not self.process or self.process.returncode is not None:
            raise RuntimeError("Engine process is not running")

        async with self.lock:
            line = json.dumps(payload) + "\n"
            self.process.stdin.write(line.encode("utf-8"))
            await self.process.stdin.drain()

            resp_line = await self.process.stdout.readline()
            if not resp_line:
                err = await self.process.stderr.read()
                raise RuntimeError(f"Engine process terminated unexpectedly: {err.decode('utf-8')}")

            return json.loads(resp_line.decode("utf-8"))

    async def stop(self):
        if self.sim_task:
            self.sim_task.cancel()
        if self.process:
            try:
                self.process.terminate()
                await self.process.wait()
            except Exception:
                pass
            print("[NexusMatch Bridge] Engine stopped.")

bridge = EngineBridge()
connected_clients: Set[WebSocket] = set()

async def broadcast(message: dict):
    if not connected_clients:
        return
    text = json.dumps(message)
    dead = set()
    for ws in connected_clients:
        try:
            await ws.send_text(text)
        except Exception:
            dead.add(ws)
    connected_clients.difference_update(dead)

async def simulation_worker():
    print("[NexusMatch Bridge] Simulation worker started.")
    mid_price = 100
    try:
        while bridge.simulating:
            await asyncio.sleep(0.08) # ~12 Hz market maker ticks
            try:
                resp = await bridge.send_command({
                    "action": "simulate",
                    "count": 5,
                    "mid_price": mid_price
                })
                # Check mid price from bbo
                bbo = resp.get("bbo", {})
                if bbo.get("bid", {}).get("valid") and bbo.get("ask", {}).get("valid"):
                    bid_p = bbo["bid"]["price"]
                    ask_p = bbo["ask"]["price"]
                    mid_price = (bid_p + ask_p) // 2
                    if mid_price <= 10:
                        mid_price = 50
                await broadcast(resp)
            except Exception as e:
                print(f"[Simulation error]: {e}")
                await asyncio.sleep(0.5)
    except asyncio.CancelledError:
        pass
    print("[NexusMatch Bridge] Simulation worker stopped.")

@asynccontextmanager
async def lifespan(app: FastAPI):
    await bridge.start()
    # Seed initial order book depth
    initial_resp = await bridge.send_command({
        "action": "seed",
        "mid_price": 100,
        "levels": 8,
        "qty": 50
    })
    print(f"[NexusMatch Bridge] Initial book seeded. Resting orders: {initial_resp.get('stats', {}).get('active_orders')}")
    yield
    await bridge.stop()

app = FastAPI(title="NexusMatch Web Terminal", lifespan=lifespan)

# REST Endpoints
class OrderRequest(BaseModel):
    action: str # "limit", "market", "cancel"
    side: Optional[str] = "buy"
    order_id: int
    owner_id: Optional[int] = 1
    price: Optional[int] = 100
    quantity: Optional[int] = 10

@app.post("/api/order")
async def place_order(order: OrderRequest):
    try:
        resp = await bridge.send_command(order.model_dump())
        await broadcast(resp)
        return resp
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/book")
async def get_book(depth: int = 20):
    try:
        return await bridge.send_command({"action": "snapshot", "depth": depth})
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/seed")
async def seed_book(mid_price: int = 100, levels: int = 8, qty: int = 50):
    try:
        resp = await bridge.send_command({
            "action": "seed",
            "mid_price": mid_price,
            "levels": levels,
            "qty": qty
        })
        await broadcast(resp)
        return resp
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/reset")
async def reset_book():
    try:
        resp = await bridge.send_command({"action": "reset"})
        # Re-seed after reset
        await bridge.send_command({"action": "seed", "mid_price": 100, "levels": 8, "qty": 50})
        snapshot = await bridge.send_command({"action": "snapshot", "depth": 20})
        await broadcast(snapshot)
        return snapshot
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/simulation/toggle")
async def toggle_simulation():
    bridge.simulating = not bridge.simulating
    if bridge.simulating:
        if not bridge.sim_task or bridge.sim_task.done():
            bridge.sim_task = asyncio.create_task(simulation_worker())
    else:
        if bridge.sim_task:
            bridge.sim_task.cancel()
    return {"simulating": bridge.simulating}

# WebSocket Endpoint
@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    connected_clients.add(websocket)
    try:
        # Send immediate initial book snapshot
        initial_book = await bridge.send_command({"action": "snapshot", "depth": 20})
        await websocket.send_text(json.dumps(initial_book))

        while True:
            text = await websocket.receive_text()
            try:
                cmd = json.loads(text)
                resp = await bridge.send_command(cmd)
                await broadcast(resp)
            except json.JSONDecodeError:
                await websocket.send_text(json.dumps({"status": "error", "message": "invalid json"}))
    except WebSocketDisconnect:
        connected_clients.discard(websocket)
    except Exception as e:
        connected_clients.discard(websocket)

# Mount static web directory
app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")

@app.get("/")
async def root():
    return FileResponse(STATIC_DIR / "index.html")

def find_available_port(start_port=8088, max_attempts=20):
    import socket
    for p in range(start_port, start_port + max_attempts):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            try:
                s.bind(("0.0.0.0", p))
                return p
            except OSError:
                continue
    return start_port

if __name__ == "__main__":
    import uvicorn
    import argparse

    parser = argparse.ArgumentParser(description="NexusMatch Web Terminal Server")
    parser.add_argument("--port", type=int, default=None, help="Port to bind (default: auto-find starting from 8088)")
    args = parser.parse_args()

    port = args.port if args.port else find_available_port(8088)

    print(f"===========================================================")
    print(f"  NEXUSMATCH HIGH-FREQUENCY MATCHING ENGINE WEB TERMINAL  ")
    print(f"  Listening on: http://localhost:{port}                    ")
    print(f"===========================================================")
    uvicorn.run(app, host="0.0.0.0", port=port, log_level="info")
