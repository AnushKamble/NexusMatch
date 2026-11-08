# NexusMatch Command WAL Format & Durability Specification

## 1. File Layout

A NexusMatch WAL file begins with a fixed 16-byte header followed by zero or more variable-length command records.

```text
+--------------------------------------------------------------+
| Header (16 bytes)                                            |
|   Magic: 0x4E584D57 ("NXMW") (4 bytes)                       |
|   Version: 1 (2 bytes)                                       |
|   Header Size: 16 (2 bytes)                                  |
|   Reserved: 0 (8 bytes)                                      |
+--------------------------------------------------------------+
| Record 1 (38 bytes)                                          |
|   Payload Length: 32 (2 bytes)                               |
|   Payload (32 bytes):                                        |
|     Command Sequence (8 bytes)                               |
|     Order Kind (1 byte)                                      |
|     Side (1 byte)                                            |
|     Reserved (2 bytes)                                       |
|     Owner ID (4 bytes)                                       |
|     Order ID (8 bytes)                                       |
|     Price Tick (4 bytes)                                     |
|     Quantity (4 bytes)                                       |
|   CRC32C Checksum (4 bytes)                                  |
+--------------------------------------------------------------+
| Record 2 ...                                                 |
```

All integer fields are encoded strictly in **Little-Endian** byte order. Raw C++ memory structures are never dumped to disk to avoid compiler padding or ABI incompatibilities.

---

## 2. Checksum and Integrity Protection

- Each record payload is checksummed using **CRC32C (Castagnoli)** polynomial `0x82F63B78`.
- If a checksum fails, the reader immediately stops with `WalReadStatus::ChecksumMismatch` (fail-closed; no silent skipping).
- Partial writes at end-of-file trigger `WalReadStatus::TruncatedRecord`.

---

## 3. Durability Guarantees & Boundaries

- **In-Memory Buffering:** `CommandWalWriter::append()` writes to the file stream buffer.
- **Process Flush:** `CommandWalWriter::flush()` flushes userspace buffers to the operating system kernel page cache.
- **Hardware Sync:** `CommandWalWriter::sync()` issues POSIX `fdatasync()`, committing kernel dirty pages to non-volatile physical storage.
- **Matching Separation:** File I/O and synchronization are completely isolated from the single-threaded matching core. The matching core never performs blocking system calls.
