# TimingGuard ⏱ — SDC Constraint Validator & STA Analyzer

> **Hero project for NVIDIA Timing Team applications**
> C++ SDC Parser · Python FastAPI · React Dashboard · MMMC Support

---

## What It Does
- **Parses** full SDC v2.1 constraint files (create_clock, set_input/output_delay, MCP, false paths, uncertainty)
- **Validates** constraints with 10+ rule checks
- **Simulates** static timing analysis (WNS, TNS, critical paths)
- **MMMC** — analyzes ss/tt/ff process corners with PVT derates
- **Suggests** ECO fixes for every violation

## Stack
| Layer | Tech |
|-------|------|
| Parser | C++ (tokenizer + recursive descent) |
| Backend | Python FastAPI |
| Frontend | React-style HTML/JS Dashboard |
| Deploy | Vercel (frontend) + Render (backend) |
| Container | Docker + docker-compose |

## Violation Types Detected
| Code | Type | Severity |
|------|------|----------|
| CLK_001 | No clocks defined | ERROR |
| CLK_002 | Invalid clock period | ERROR |
| CLK_003 | Overly aggressive frequency | WARNING |
| IO_001 | Unconstrained inputs | WARNING |
| IO_002 | Unconstrained outputs | WARNING |
| IO_003 | Input delay > 60% period | WARNING |
| UNC_001 | Missing clock uncertainty | WARNING |
| MCP_001 | MCP setup without hold comp | WARNING |
| CDC_001 | CDC paths without exceptions | WARNING |
| STA_001 | Negative slack (WNS < 0) | ERROR |
| STA_002 | TNS < -1ns | ERROR |

## Run Locally
```bash
# Backend
cd backend && pip install -r requirements.txt
uvicorn main:app --reload --port 8000

# Frontend
open frontend/timingguard.html   # or any static server
```

## Deploy
```bash
# Docker
cd docker && docker-compose up --build

# Vercel (frontend)
vercel deploy

# Render (backend) — set start command:
uvicorn main:app --host 0.0.0.0 --port $PORT
```

## NVIDIA Keywords
`SDC` · `STA` · `WNS` · `TNS` · `MMMC` · `Clock Uncertainty` · `Multicycle Path` · `False Path` · `CDC` · `ECO` · `OpenSTA` · `PVT corners`
