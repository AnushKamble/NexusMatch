// NexusMatch Web Terminal Client
// Connects via WebSocket to C++ matching engine bridge

class NexusMatchClient {
    constructor() {
        this.ws = null;
        this.reconnectAttempts = 0;
        this.currentSide = 'buy';
        this.currentType = 'limit';
        this.nextOrderId = 1001;
        this.myRestingOrders = new Map(); // order_id -> {id, side, price, qty}
        this.recentTrades = [];
        this.recentEvents = [];
        this.latestDepth = { bids: [], asks: [] };
        this.latestBBO = { bid: { price: 0, qty: 0, valid: false }, ask: { price: 0, qty: 0, valid: false }, spread: 0 };
        this.isSimulating = false;

        this.initElements();
        this.bindEvents();
        this.connectWebSocket();
        this.initCanvas();
    }

    initElements() {
        // Telemetry header
        this.elEngineStatus = document.getElementById('engineStatus');
        this.elLastLatency = document.getElementById('lastLatency');
        this.elInvariantsBadge = document.getElementById('invariantsBadge');
        this.elStateHash = document.getElementById('stateHash');
        this.elArenaGauge = document.getElementById('arenaGauge');

        // BBO
        this.elBboBidPrice = document.getElementById('bboBidPrice');
        this.elBboBidQty = document.getElementById('bboBidQty');
        this.elBboAskPrice = document.getElementById('bboAskPrice');
        this.elBboAskQty = document.getElementById('bboAskQty');
        this.elBboSpread = document.getElementById('bboSpread');
        this.elBboMid = document.getElementById('bboMid');
        this.elDividerSpread = document.getElementById('dividerSpreadText');

        // Ladders
        this.elLadderAsks = document.getElementById('ladderAsks');
        this.elLadderBids = document.getElementById('ladderBids');

        // Form
        this.elSideBuy = document.getElementById('btnSideBuy');
        this.elSideSell = document.getElementById('btnSideSell');
        this.elInputPrice = document.getElementById('inputPrice');
        this.elInputQty = document.getElementById('inputQty');
        this.elInputOwner = document.getElementById('inputOwner');
        this.elInputId = document.getElementById('inputId');
        this.elGroupPrice = document.getElementById('groupPrice');
        this.elGroupQty = document.getElementById('groupQty');
        this.elSubmitBtn = document.getElementById('btnSubmitOrder');
        this.elSubmitLabel = document.getElementById('submitLabel');
        this.elMyOrdersTable = document.querySelector('#myOrdersTable tbody');
        this.elMyOrdersCount = document.getElementById('myOrdersCount');

        // Feeds
        this.elTradeTapeBody = document.getElementById('tradeTapeBody');
        this.elTradeCountBadge = document.getElementById('tradeCountBadge');
        this.elEventFeed = document.getElementById('eventFeed');

        // Metrics
        this.elMetricLatency = document.getElementById('metricLatency');
        this.elMetricAccepted = document.getElementById('metricAccepted');
        this.elMetricCancels = document.getElementById('metricCancels');
        this.elMetricSTP = document.getElementById('metricSTP');

        // Canvas
        this.canvas = document.getElementById('depthCanvas');
        this.ctx = this.canvas.getContext('2d');

        // Control buttons
        this.btnSeed = document.getElementById('btnSeed');
        this.btnBurst = document.getElementById('btnBurst');
        this.btnToggleSim = document.getElementById('btnToggleSim');
        this.btnReset = document.getElementById('btnReset');
        this.simBtnText = document.getElementById('simBtnText');
    }

    bindEvents() {
        // Side buttons
        this.elSideBuy.addEventListener('click', () => this.setSide('buy'));
        this.elSideSell.addEventListener('click', () => this.setSide('sell'));

        // Type pills
        document.querySelectorAll('.type-pill').forEach(pill => {
            pill.addEventListener('click', (e) => {
                document.querySelectorAll('.type-pill').forEach(p => p.classList.remove('active'));
                e.target.classList.add('active');
                this.setType(e.target.dataset.type);
            });
        });

        // Stepper buttons
        document.getElementById('btnDecPrice').addEventListener('click', () => {
            let val = parseInt(this.elInputPrice.value, 10) || 100;
            if (val > 1) this.elInputPrice.value = val - 1;
        });
        document.getElementById('btnIncPrice').addEventListener('click', () => {
            let val = parseInt(this.elInputPrice.value, 10) || 100;
            this.elInputPrice.value = val + 1;
        });
        document.getElementById('btnDecQty').addEventListener('click', () => {
            let val = parseInt(this.elInputQty.value, 10) || 10;
            if (val > 1) this.elInputQty.value = val - 1;
        });
        document.getElementById('btnIncQty').addEventListener('click', () => {
            let val = parseInt(this.elInputQty.value, 10) || 10;
            this.elInputQty.value = val + 1;
        });

        // Quick Qty chips
        document.querySelectorAll('.quick-qty-chips .chip').forEach(chip => {
            chip.addEventListener('click', () => {
                this.elInputQty.value = chip.dataset.qty;
            });
        });

        // Quick action buttons
        document.getElementById('btnJoinBid').addEventListener('click', () => {
            if (this.latestBBO.bid.valid) {
                this.setSide('buy');
                this.elInputPrice.value = this.latestBBO.bid.price;
            }
        });
        document.getElementById('btnJoinAsk').addEventListener('click', () => {
            if (this.latestBBO.ask.valid) {
                this.setSide('sell');
                this.elInputPrice.value = this.latestBBO.ask.price;
            }
        });
        document.getElementById('btnCross').addEventListener('click', () => {
            if (this.currentSide === 'buy' && this.latestBBO.ask.valid) {
                this.elInputPrice.value = this.latestBBO.ask.price;
            } else if (this.currentSide === 'sell' && this.latestBBO.bid.valid) {
                this.elInputPrice.value = this.latestBBO.bid.price;
            }
        });

        // Test Self-Trade Prevention (Cancel Aggressor Remainder)
        document.getElementById('btnTestSTP').addEventListener('click', () => this.triggerSTPTest());

        // Submit button
        this.elSubmitBtn.addEventListener('click', (e) => {
            e.preventDefault();
            this.submitCurrentOrder();
        });

        // Toolbar buttons
        this.btnSeed.addEventListener('click', () => this.sendCommand({ action: 'seed', mid_price: 100, levels: 8, qty: 50 }));
        this.btnBurst.addEventListener('click', () => this.sendCommand({ action: 'simulate', count: 50, mid_price: 100 }));
        this.btnReset.addEventListener('click', () => {
            this.myRestingOrders.clear();
            this.updateMyOrdersBlotter();
            this.sendCommand({ action: 'reset' });
        });
        this.btnToggleSim.addEventListener('click', () => this.toggleSimulation());

        window.addEventListener('resize', () => this.initCanvas());
    }

    setSide(side) {
        this.currentSide = side;
        if (side === 'buy') {
            this.elSideBuy.classList.add('active-buy');
            this.elSideSell.classList.remove('active-sell');
            this.elSubmitBtn.className = 'btn-submit btn-submit-buy';
        } else {
            this.elSideSell.classList.add('active-sell');
            this.elSideBuy.classList.remove('active-buy');
            this.elSubmitBtn.className = 'btn-submit btn-submit-sell';
        }
        this.updateSubmitLabel();
    }

    setType(type) {
        this.currentType = type;
        if (type === 'limit') {
            this.elGroupPrice.style.display = 'flex';
            this.elGroupQty.style.display = 'flex';
            this.setSide(this.currentSide);
        } else if (type === 'market') {
            this.elGroupPrice.style.display = 'none';
            this.elGroupQty.style.display = 'flex';
            this.setSide(this.currentSide);
        } else if (type === 'cancel') {
            this.elGroupPrice.style.display = 'none';
            this.elGroupQty.style.display = 'none';
            this.elSubmitBtn.className = 'btn-submit btn-submit-cancel';
        }
        this.updateSubmitLabel();
    }

    updateSubmitLabel() {
        if (this.currentType === 'limit') {
            this.elSubmitLabel.textContent = `SUBMIT LIMIT ${this.currentSide.toUpperCase()}`;
        } else if (this.currentType === 'market') {
            this.elSubmitLabel.textContent = `SWEEP MARKET ${this.currentSide.toUpperCase()}`;
        } else if (this.currentType === 'cancel') {
            this.elSubmitLabel.textContent = `CANCEL ORDER #${this.elInputId.value}`;
        }
    }

    connectWebSocket() {
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const wsUrl = `${protocol}//${window.location.host}/ws`;

        this.elEngineStatus.textContent = 'CONNECTING...';
        this.elEngineStatus.className = 'pill-val text-highlight';

        this.ws = new WebSocket(wsUrl);

        this.ws.onopen = () => {
            this.reconnectAttempts = 0;
            this.elEngineStatus.textContent = 'ONLINE (C++ CORE)';
            this.elEngineStatus.className = 'pill-val text-green';
            this.addEventLog('SYSTEM', 'WebSocket connection established with C++ engine bridge.', 'info');
        };

        this.ws.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                this.handleEngineMessage(data);
            } catch (err) {
                console.error('Failed to parse WebSocket message:', err);
            }
        };

        this.ws.onclose = () => {
            this.elEngineStatus.textContent = 'DISCONNECTED';
            this.elEngineStatus.className = 'pill-val text-red';
            const delay = Math.min(3000, 1000 * Math.pow(1.5, this.reconnectAttempts++));
            setTimeout(() => this.connectWebSocket(), delay);
        };
    }

    sendCommand(cmd) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify(cmd));
        } else {
            // Fallback to fetch REST if WebSocket is momentarily closed
            fetch('/api/order', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(cmd)
            });
        }
    }

    submitCurrentOrder() {
        const orderId = parseInt(this.elInputId.value, 10) || this.nextOrderId++;
        const ownerId = parseInt(this.elInputOwner.value, 10) || 1;
        const price = parseInt(this.elInputPrice.value, 10) || 100;
        const qty = parseInt(this.elInputQty.value, 10) || 10;

        const cmd = {
            action: this.currentType,
            side: this.currentSide,
            order_id: orderId,
            owner_id: ownerId,
            price: price,
            quantity: qty
        };

        this.sendCommand(cmd);

        // Auto-increment order ID input for next trade
        this.elInputId.value = orderId + 1;
    }

    triggerSTPTest() {
        // To demonstrate STP: Place a resting order, then place an aggressive matching order with the SAME owner_id!
        const owner = 42;
        const testId1 = this.nextOrderId++;
        const testId2 = this.nextOrderId++;
        this.elInputId.value = this.nextOrderId;

        this.addEventLog('DEMO-STP', `Step 1: Submitting resting Maker BUY at tick 100 (Owner: ${owner})...`, 'info');
        this.sendCommand({
            action: 'limit',
            side: 'buy',
            order_id: testId1,
            owner_id: owner,
            price: 100,
            quantity: 20
        });

        setTimeout(() => {
            this.addEventLog('DEMO-STP', `Step 2: Submitting matching Aggressor SELL at tick 100 with SAME Owner (${owner})!`, 'info');
            this.sendCommand({
                action: 'limit',
                side: 'sell',
                order_id: testId2,
                owner_id: owner,
                price: 100,
                quantity: 20
            });
        }, 150);
    }

    cancelOrder(orderId) {
        this.sendCommand({
            action: 'cancel',
            order_id: orderId
        });
    }

    toggleSimulation() {
        fetch('/api/simulation/toggle', { method: 'POST' })
            .then(res => res.json())
            .then(data => {
                this.isSimulating = data.simulating;
                if (this.isSimulating) {
                    this.simBtnText.textContent = 'Stop Market Maker';
                    this.btnToggleSim.classList.remove('btn-primary');
                    this.btnToggleSim.classList.add('btn-danger-outline');
                } else {
                    this.simBtnText.textContent = 'Start Market Maker';
                    this.btnToggleSim.classList.remove('btn-danger-outline');
                    this.btnToggleSim.classList.add('btn-primary');
                }
            });
    }

    handleEngineMessage(data) {
        if (!data) return;

        // Latency
        if (data.latency_ns !== undefined) {
            this.elLastLatency.textContent = `${data.latency_ns} ns`;
            this.elMetricLatency.textContent = `${data.latency_ns} ns`;
        }

        // BBO
        if (data.bbo) {
            this.latestBBO = data.bbo;
            this.renderBBO(data.bbo);
        }

        // Depth
        if (data.depth) {
            this.latestDepth = data.depth;
            this.renderLadder(data.depth);
            this.renderDepthChart(data.depth);
        }

        // Stats & Invariants
        if (data.stats) {
            this.renderStats(data.stats);
        }

        // Events
        if (data.events && data.events.length > 0) {
            data.events.forEach(ev => this.processEngineEvent(ev));
        }
    }

    renderBBO(bbo) {
        if (bbo.bid && bbo.bid.valid) {
            this.elBboBidPrice.textContent = bbo.bid.price;
            this.elBboBidQty.textContent = `${bbo.bid.qty.toLocaleString()} qty`;
        } else {
            this.elBboBidPrice.textContent = '--';
            this.elBboBidQty.textContent = '0 qty';
        }

        if (bbo.ask && bbo.ask.valid) {
            this.elBboAskPrice.textContent = bbo.ask.price;
            this.elBboAskQty.textContent = `${bbo.ask.qty.toLocaleString()} qty`;
        } else {
            this.elBboAskPrice.textContent = '--';
            this.elBboAskQty.textContent = '0 qty';
        }

        if (bbo.bid && bbo.ask && bbo.bid.valid && bbo.ask.valid) {
            const spread = bbo.spread || (bbo.ask.price - bbo.bid.price);
            const mid = ((bbo.bid.price + bbo.ask.price) / 2).toFixed(1);
            this.elBboSpread.textContent = `${spread} ticks`;
            this.elBboMid.textContent = `MID: ${mid}`;
            this.elDividerSpread.textContent = `${spread} ticks`;
        } else {
            this.elBboSpread.textContent = '-- ticks';
            this.elBboMid.textContent = 'MID: --';
            this.elDividerSpread.textContent = '--';
        }
    }

    renderLadder(depth) {
        // Calculate max volume for proportional bars
        let maxQty = 1;
        const asks = (depth.asks || []).slice(0, 15);
        const bids = (depth.bids || []).slice(0, 15);

        asks.forEach(a => { if (a.qty > maxQty) maxQty = a.qty; });
        bids.forEach(b => { if (b.qty > maxQty) maxQty = b.qty; });

        // Render Asks (Reversed so lowest ask is at the bottom nearest spread)
        if (asks.length === 0) {
            this.elLadderAsks.innerHTML = '<div class="empty-ladder">No asks resting in book</div>';
        } else {
            const reversedAsks = [...asks].reverse();
            this.elLadderAsks.innerHTML = reversedAsks.map(a => {
                const pct = Math.min(100, (a.qty / maxQty) * 100);
                return `
                    <div class="ladder-row ladder-ask-row" onclick="client.selectPrice(${a.price})">
                        <div class="row-bar" style="width: ${pct}%"></div>
                        <div class="col-count">${a.orders}</div>
                        <div class="col-size">${a.qty.toLocaleString()}</div>
                        <div class="col-bar"></div>
                        <div class="col-price">${a.price}</div>
                    </div>
                `;
            }).join('');
        }

        // Render Bids (Highest bid at top nearest spread)
        if (bids.length === 0) {
            this.elLadderBids.innerHTML = '<div class="empty-ladder">No bids resting in book</div>';
        } else {
            this.elLadderBids.innerHTML = bids.map(b => {
                const pct = Math.min(100, (b.qty / maxQty) * 100);
                return `
                    <div class="ladder-row ladder-bid-row" onclick="client.selectPrice(${b.price})">
                        <div class="row-bar" style="width: ${pct}%"></div>
                        <div class="col-count">${b.orders}</div>
                        <div class="col-size">${b.qty.toLocaleString()}</div>
                        <div class="col-bar"></div>
                        <div class="col-price">${b.price}</div>
                    </div>
                `;
            }).join('');
        }
    }

    selectPrice(price) {
        this.elInputPrice.value = price;
    }

    renderStats(stats) {
        this.elMetricAccepted.textContent = (stats.total_accepted || 0).toLocaleString();
        this.elMetricCancels.textContent = (stats.total_cancels || 0).toLocaleString();
        this.elMetricSTP.textContent = (stats.total_self_trades_prevented || 0).toLocaleString();

        if (stats.arena_capacity) {
            this.elArenaGauge.textContent = `${(stats.active_orders || 0).toLocaleString()} / ${stats.arena_capacity.toLocaleString()}`;
        }
        if (stats.state_hash) {
            this.elStateHash.textContent = stats.state_hash.length > 12 ? stats.state_hash.substring(0, 12) + '...' : stats.state_hash;
            this.elStateHash.title = `Canonical FNV-1a State Hash: ${stats.state_hash}`;
        }
        if (stats.invariants_valid !== undefined) {
            this.elInvariantsBadge.textContent = stats.invariants_valid ? '18 / 18 PASS' : 'FAIL';
            this.elInvariantsBadge.className = stats.invariants_valid ? 'pill-val text-green' : 'pill-val text-red';
        }
    }

    processEngineEvent(ev) {
        if (!ev) return;

        if (ev.type === 'Trade') {
            this.addTradeTapeRow(ev);
            this.addEventLog('TRADE', `MATCH #${ev.trade_seq}: ${ev.quantity} units @ tick ${ev.price_tick} (Maker #${ev.maker_order_id} vs Taker #${ev.taker_order_id})`, 'trade');
        } else if (ev.type === 'OrderRested') {
            this.myRestingOrders.set(ev.order_id, {
                id: ev.order_id,
                side: ev.side,
                price: ev.price_tick,
                qty: ev.remaining_qty
            });
            this.updateMyOrdersBlotter();
            this.addEventLog('RESTED', `Order #${ev.order_id} rested ${ev.side.toUpperCase()} ${ev.remaining_qty} @ ${ev.price_tick}`, 'rested');
        } else if (ev.type === 'OrderCancelled') {
            this.myRestingOrders.delete(ev.order_id);
            this.updateMyOrdersBlotter();
            this.addEventLog('CANCEL', `Order #${ev.order_id} cancelled (${ev.cancelled_qty} units unlinked)`, 'cancel');
        } else if (ev.type === 'SelfTradePrevented') {
            this.myRestingOrders.delete(ev.taker_order_id);
            this.updateMyOrdersBlotter();
            this.addEventLog('STP', `SELF-TRADE PREVENTED! Aggressor #${ev.taker_order_id} cancelled remainder (${ev.cancelled_qty} units). Maker #${ev.maker_order_id} preserved!`, 'stp');
        } else if (ev.type === 'ResidualCancelled') {
            this.myRestingOrders.delete(ev.order_id);
            this.updateMyOrdersBlotter();
            this.addEventLog('MARKET', `Market order #${ev.order_id} residual cancelled (${ev.cancelled_qty} units unfilled)`, 'cancel');
        } else if (ev.type === 'CancelRejected') {
            this.addEventLog('REJECT', `Cancel rejected for order #${ev.order_id}: ${ev.reason}`, 'cancel');
        } else if (ev.type === 'OrderRejected') {
            this.addEventLog('REJECT', `Order #${ev.order_id} rejected by engine`, 'cancel');
        }
    }

    addTradeTapeRow(trade) {
        this.recentTrades.unshift(trade);
        if (this.recentTrades.length > 40) this.recentTrades.pop();

        this.elTradeCountBadge.textContent = `${this.recentTrades.length} FILLS`;

        const rows = this.recentTrades.map((t, idx) => {
            const sideClass = t.maker_side === 'buy' ? 'text-green' : 'text-red';
            const flashClass = idx === 0 ? 'tape-flash' : '';
            return `
                <tr class="${flashClass}">
                    <td class="font-mono">${t.trade_seq || '-'}</td>
                    <td class="font-mono font-bold ${sideClass}">${t.price_tick}</td>
                    <td class="font-mono">${t.quantity}</td>
                    <td class="font-mono ${sideClass}">${t.maker_side.toUpperCase()}</td>
                    <td class="font-mono text-muted">#${t.maker_order_id} &rarr; #${t.taker_order_id}</td>
                </tr>
            `;
        }).join('');

        this.elTradeTapeBody.innerHTML = rows;
    }

    addEventLog(tag, desc, type) {
        const item = document.createElement('div');
        item.className = `event-item event-${type}`;
        item.innerHTML = `<span class="event-tag">${tag}</span><span class="event-desc">${desc}</span>`;
        this.elEventFeed.insertBefore(item, this.elEventFeed.firstChild);

        while (this.elEventFeed.children.length > 50) {
            this.elEventFeed.removeChild(this.elEventFeed.lastChild);
        }
    }

    updateMyOrdersBlotter() {
        this.elMyOrdersCount.textContent = this.myRestingOrders.size;
        if (this.myRestingOrders.size === 0) {
            this.elMyOrdersTable.innerHTML = '<tr class="empty-row"><td colspan="5">No active resting orders</td></tr>';
            return;
        }

        const rows = Array.from(this.myRestingOrders.values()).reverse().map(o => {
            const sideColor = o.side === 'buy' ? 'text-green' : 'text-red';
            return `
                <tr>
                    <td class="font-mono">#${o.id}</td>
                    <td class="font-mono ${sideColor}">${o.side.toUpperCase()}</td>
                    <td class="font-mono">${o.price}</td>
                    <td class="font-mono">${o.qty}</td>
                    <td><button class="btn-row-cancel" onclick="client.cancelOrder(${o.id})">Cancel</button></td>
                </tr>
            `;
        }).join('');

        this.elMyOrdersTable.innerHTML = rows;
    }

    // Canvas Depth Chart Drawing
    initCanvas() {
        const rect = this.canvas.getBoundingClientRect();
        this.canvas.width = rect.width * window.devicePixelRatio;
        this.canvas.height = rect.height * window.devicePixelRatio;
        this.ctx.scale(window.devicePixelRatio, window.devicePixelRatio);
        this.renderDepthChart(this.latestDepth);
    }

    renderDepthChart(depth) {
        if (!this.ctx) return;
        const w = this.canvas.getBoundingClientRect().width;
        const h = this.canvas.getBoundingClientRect().height;

        this.ctx.clearRect(0, 0, w, h);

        const bids = depth.bids || [];
        const asks = depth.asks || [];
        if (bids.length === 0 && asks.length === 0) {
            this.ctx.fillStyle = '#64748b';
            this.ctx.font = '11px JetBrains Mono';
            this.ctx.textAlign = 'center';
            this.ctx.fillText('Awaiting order book depth...', w / 2, h / 2);
            return;
        }

        // Calculate cumulative totals
        let cumBids = [];
        let cumTotal = 0;
        for (let b of bids) {
            cumTotal += b.qty;
            cumBids.push({ price: b.price, cum: cumTotal });
        }

        let cumAsks = [];
        cumTotal = 0;
        for (let a of asks) {
            cumTotal += a.qty;
            cumAsks.push({ price: a.price, cum: cumTotal });
        }

        const maxCum = Math.max(
            cumBids.length > 0 ? cumBids[cumBids.length - 1].cum : 1,
            cumAsks.length > 0 ? cumAsks[cumAsks.length - 1].cum : 1
        );

        const midX = w / 2;

        // Draw Bids (Left half, green)
        if (cumBids.length > 0) {
            this.ctx.beginPath();
            this.ctx.moveTo(midX, h);
            for (let i = 0; i < cumBids.length; ++i) {
                const x = midX - ((i + 1) / cumBids.length) * (midX - 10);
                const y = h - (cumBids[i].cum / maxCum) * (h - 20);
                this.ctx.lineTo(x, y);
            }
            this.ctx.lineTo(0, h);
            this.ctx.closePath();

            const gradBid = this.ctx.createLinearGradient(0, 0, 0, h);
            gradBid.addColorStop(0, 'rgba(0, 230, 118, 0.45)');
            gradBid.addColorStop(1, 'rgba(0, 230, 118, 0.05)');
            this.ctx.fillStyle = gradBid;
            this.ctx.fill();

            this.ctx.strokeStyle = '#00e676';
            this.ctx.lineWidth = 2;
            this.ctx.stroke();
        }

        // Draw Asks (Right half, red)
        if (cumAsks.length > 0) {
            this.ctx.beginPath();
            this.ctx.moveTo(midX, h);
            for (let i = 0; i < cumAsks.length; ++i) {
                const x = midX + ((i + 1) / cumAsks.length) * (midX - 10);
                const y = h - (cumAsks[i].cum / maxCum) * (h - 20);
                this.ctx.lineTo(x, y);
            }
            this.ctx.lineTo(w, h);
            this.ctx.closePath();

            const gradAsk = this.ctx.createLinearGradient(0, 0, 0, h);
            gradAsk.addColorStop(0, 'rgba(255, 51, 102, 0.45)');
            gradAsk.addColorStop(1, 'rgba(255, 51, 102, 0.05)');
            this.ctx.fillStyle = gradAsk;
            this.ctx.fill();

            this.ctx.strokeStyle = '#ff3366';
            this.ctx.lineWidth = 2;
            this.ctx.stroke();
        }
    }
}

// Instantiate global client
let client;
document.addEventListener('DOMContentLoaded', () => {
    client = new NexusMatchClient();
});
