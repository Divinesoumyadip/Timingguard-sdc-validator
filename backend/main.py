"""
TimingGuard Backend — SDC Constraint Validator & STA Analyzer
FastAPI + Python simulation of C++ SDC parser
"""
from __future__ import annotations
import re
import math
import random
import uuid
from datetime import datetime
from enum import Enum
from typing import Any, Dict, List, Optional, Tuple

from fastapi import FastAPI, HTTPException, UploadFile, File
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

app = FastAPI(
    title="TimingGuard — SDC Validator & STA Analyzer",
    description="Parses SDC files, flags timing violations, suggests fixes, supports MMMC",
    version="1.4.0",
)
app.add_middleware(CORSMiddleware, allow_origins=["*"], allow_methods=["*"], allow_headers=["*"])

# ─── Models ──────────────────────────────────────────────────────────────────

class Severity(str, Enum):
    ERROR   = "ERROR"
    WARNING = "WARNING"
    INFO    = "INFO"

class ViolationType(str, Enum):
    MISSING_CLOCK          = "MISSING_CLOCK"
    UNCONSTRAINED_INPUT    = "UNCONSTRAINED_INPUT"
    UNCONSTRAINED_OUTPUT   = "UNCONSTRAINED_OUTPUT"
    OVERLY_TIGHT           = "OVERLY_TIGHT_CONSTRAINT"
    CONFLICTING            = "CONFLICTING_CONSTRAINTS"
    MISSING_UNCERTAINTY    = "MISSING_UNCERTAINTY"
    INVALID_MULTICYCLE     = "INVALID_MULTICYCLE"
    CDC                    = "CLOCK_DOMAIN_CROSSING"
    NEGATIVE_SLACK         = "NEGATIVE_SLACK"
    TNS_VIOLATION          = "TNS_VIOLATION"
    MMMC_MISMATCH          = "MMMC_CORNER_MISMATCH"

class ClockDef(BaseModel):
    name: str
    source_port: str = ""
    period_ns: float
    waveform_rise: float = 0.0
    waveform_fall: float = 0.0
    is_virtual: bool = False
    is_generated: bool = False
    master_clock: str = ""
    divide_by: float = 1.0
    multiply_by: float = 1.0
    frequency_mhz: float = 0.0
    line_number: int = 0

class Violation(BaseModel):
    id: str
    type: ViolationType
    severity: Severity
    message: str
    suggestion: str
    line_number: int = 0
    constraint_id: str = ""
    value: float = 0.0

class TimingPath(BaseModel):
    startpoint: str
    endpoint: str
    clock: str
    arrival_time_ns: float
    required_time_ns: float
    slack_ns: float
    is_setup: bool = True
    path_type: str = "max"

class MMMCCorner(BaseModel):
    name: str
    process: str   # ff, tt, ss
    voltage: float
    temperature: float
    wns_ns: float = 0.0
    tns_ns: float = 0.0
    timing_met: bool = True

class SDCAnalysisResult(BaseModel):
    analysis_id: str
    filename: str
    total_lines: int
    clocks: List[ClockDef]
    input_delay_count: int
    output_delay_count: int
    false_path_count: int
    multicycle_path_count: int
    uncertainty_count: int
    violations: List[Violation]
    timing_paths: List[TimingPath]
    mmmc_corners: List[MMMCCorner]
    error_count: int
    warning_count: int
    info_count: int
    worst_slack_ns: float
    total_negative_slack_ns: float
    timing_met: bool
    coverage_score: float   # 0-100
    analyzed_at: str

class AnalyzeRequest(BaseModel):
    sdc_content: str
    filename: str = "design.sdc"
    enable_mmmc: bool = True
    design_name: str = "riscv_core"

# ─── SDC Parser (Python mirror of C++ engine) ─────────────────────────────────

def parse_sdc(content: str, filename: str, enable_mmmc: bool, design_name: str) -> SDCAnalysisResult:
    lines = content.strip().split('\n')
    clocks: List[ClockDef] = []
    input_delays = []
    output_delays = []
    false_paths = []
    multicycle_paths = []
    uncertainties = []
    violations: List[Violation] = []
    vid = 0

    def new_violation(vtype, sev, msg, suggestion, line_no=0, constraint_id="", value=0.0):
        nonlocal vid
        vid += 1
        return Violation(
            id=f"V{vid:03d}", type=vtype, severity=sev,
            message=msg, suggestion=suggestion,
            line_number=line_no, constraint_id=constraint_id, value=value
        )

    def tokenize(line):
        line = line.split('#')[0].strip()
        tokens = []
        cur = ''
        bracket = 0
        for c in line:
            if c == '[': bracket += 1; cur += c
            elif c == ']': bracket -= 1; cur += c
            elif c in (' ', '\t') and bracket == 0:
                if cur: tokens.append(cur); cur = ''
            else: cur += c
        if cur: tokens.append(cur)
        return tokens

    def get_flag(tokens, flag, default=None):
        try:
            idx = tokens.index(flag)
            return tokens[idx+1] if idx+1 < len(tokens) else default
        except ValueError:
            return default

    def has_flag(tokens, flag):
        return flag in tokens

    def get_num(tokens, flag, default=0.0):
        val = get_flag(tokens, flag)
        if val:
            try: return float(val)
            except: pass
        # Try first standalone number after command
        for t in tokens[1:]:
            if t[0] != '-':
                try: return float(t)
                except: pass
        return default

    for line_no, raw_line in enumerate(lines, 1):
        tokens = tokenize(raw_line)
        if not tokens: continue
        cmd = tokens[0]

        if cmd == 'create_clock':
            period = get_num(tokens, '-period', 0.0)
            name   = get_flag(tokens, '-name', 'clk')
            src    = tokens[-1] if tokens[-1][0] != '-' else ''
            freq   = round(1000.0 / period, 2) if period > 0 else 0.0
            clocks.append(ClockDef(
                name=name, source_port=src, period_ns=period,
                frequency_mhz=freq, line_number=line_no
            ))

        elif cmd == 'create_generated_clock':
            period  = get_num(tokens, '-period', 2.5)
            name    = get_flag(tokens, '-name', 'clk_gen')
            master  = get_flag(tokens, '-master_clock', '')
            div_by  = get_num(tokens, '-divide_by', 1.0)
            mul_by  = get_num(tokens, '-multiply_by', 1.0)
            eff_per = period * div_by / max(mul_by, 1.0)
            clocks.append(ClockDef(
                name=name, period_ns=eff_per, is_generated=True,
                master_clock=master, divide_by=div_by, multiply_by=mul_by,
                frequency_mhz=round(1000.0/eff_per, 2) if eff_per > 0 else 0,
                line_number=line_no
            ))

        elif cmd == 'set_input_delay':
            delay = get_num(tokens, '-max', get_num(tokens, '-min', 0.5))
            clk   = get_flag(tokens, '-clock', '')
            port  = tokens[-1] if tokens[-1][0] != '-' else '[all_inputs]'
            input_delays.append({'delay': delay, 'clock': clk, 'port': port, 'line': line_no})

        elif cmd == 'set_output_delay':
            delay = get_num(tokens, '-max', get_num(tokens, '-min', 0.5))
            clk   = get_flag(tokens, '-clock', '')
            port  = tokens[-1] if tokens[-1][0] != '-' else '[all_outputs]'
            output_delays.append({'delay': delay, 'clock': clk, 'port': port, 'line': line_no})

        elif cmd == 'set_clock_uncertainty':
            val   = get_num(tokens, '-setup', get_num(tokens, '', 0.1))
            hold  = get_num(tokens, '-hold', val * 0.5)
            clk   = get_flag(tokens, '-from', get_flag(tokens, '-to', 'all_clocks'))
            uncertainties.append({'setup': val, 'hold': hold, 'clock': clk, 'line': line_no})

        elif cmd == 'set_false_path':
            false_paths.append({
                'from': get_flag(tokens, '-from', ''),
                'to':   get_flag(tokens, '-to', ''),
                'line': line_no
            })

        elif cmd == 'set_multicycle_path':
            cycles = 2
            for t in reversed(tokens[1:]):
                try: cycles = int(t); break
                except: pass
            is_setup = not has_flag(tokens, '-hold')
            multicycle_paths.append({
                'cycles': cycles, 'setup': is_setup,
                'from': get_flag(tokens, '-from', ''),
                'to':   get_flag(tokens, '-to', ''),
                'line': line_no
            })

    # ── Validation Passes ─────────────────────────────────────────────────────

    # V1: Clock presence
    if not clocks:
        violations.append(new_violation(
            ViolationType.MISSING_CLOCK, Severity.ERROR,
            "No clocks defined in SDC file.",
            "Add: create_clock -name clk -period 2.5 [get_ports clk]",
            constraint_id="CLK_001"
        ))
    else:
        for clk in clocks:
            if clk.period_ns <= 0:
                violations.append(new_violation(
                    ViolationType.MISSING_CLOCK, Severity.ERROR,
                    f"Clock '{clk.name}' has invalid period: {clk.period_ns} ns",
                    f"Specify -period > 0.  E.g. create_clock -name {clk.name} -period 2.5",
                    clk.line_number, "CLK_002", clk.period_ns
                ))
            if 0 < clk.period_ns < 0.5:
                violations.append(new_violation(
                    ViolationType.OVERLY_TIGHT, Severity.WARNING,
                    f"Clock '{clk.name}' period {clk.period_ns:.2f}ns implies >{1000/clk.period_ns:.0f}MHz — very aggressive.",
                    "Verify target frequency. sky130 PDK practical max ~200MHz.",
                    clk.line_number, "CLK_003", clk.period_ns
                ))

    # V2: I/O constraints
    if not input_delays:
        violations.append(new_violation(
            ViolationType.UNCONSTRAINED_INPUT, Severity.WARNING,
            "No set_input_delay found — all inputs are unconstrained.",
            "Add: set_input_delay -max 0.5 -clock clk [all_inputs]",
            constraint_id="IO_001"
        ))
    if not output_delays:
        violations.append(new_violation(
            ViolationType.UNCONSTRAINED_OUTPUT, Severity.WARNING,
            "No set_output_delay found — all outputs are unconstrained.",
            "Add: set_output_delay -max 0.5 -clock clk [all_outputs]",
            constraint_id="IO_002"
        ))

    # V3: Overly tight I/O delays
    for inp in input_delays:
        for clk in clocks:
            if inp['clock'] == clk.name and clk.period_ns > 0 and inp['delay'] > clk.period_ns * 0.6:
                violations.append(new_violation(
                    ViolationType.OVERLY_TIGHT, Severity.WARNING,
                    f"Input delay {inp['delay']}ns on '{inp['port']}' is >60% of clock period {clk.period_ns}ns.",
                    f"Rule of thumb: input_delay ≤ 0.4 × period = {clk.period_ns*0.4:.2f}ns",
                    inp['line'], "IO_003", inp['delay']
                ))

    # V4: Missing uncertainty
    if not uncertainties:
        violations.append(new_violation(
            ViolationType.MISSING_UNCERTAINTY, Severity.WARNING,
            "No set_clock_uncertainty — jitter and skew not modeled.",
            "Add: set_clock_uncertainty -setup 0.1 -hold 0.05 [all_clocks]",
            constraint_id="UNC_001"
        ))

    # V5: Multicycle path hold compensation
    setup_mcps = [m for m in multicycle_paths if m['setup'] and m['cycles'] > 1]
    hold_mcps  = [m for m in multicycle_paths if not m['setup']]
    for mcp in setup_mcps:
        has_hold = any(h['from'] == mcp['from'] for h in hold_mcps)
        if not has_hold:
            violations.append(new_violation(
                ViolationType.INVALID_MULTICYCLE, Severity.WARNING,
                f"MCP setup={mcp['cycles']} from '{mcp['from']}' has no hold compensation.",
                f"Add: set_multicycle_path -hold {mcp['cycles']-1} -from {mcp['from']}",
                mcp['line'], "MCP_001"
            ))

    # V6: CDC detection
    clk_names_in_io = set(
        [i['clock'] for i in input_delays] + [o['clock'] for o in output_delays]
    )
    if len(clk_names_in_io) > 1 and not false_paths and not multicycle_paths:
        violations.append(new_violation(
            ViolationType.CDC, Severity.WARNING,
            f"Multiple clock domains in I/O constraints ({len(clk_names_in_io)} clocks) with no CDC exceptions.",
            "Add: set_false_path -from [get_clocks clk_a] -to [get_clocks clk_b] for async crossings.",
            constraint_id="CDC_001"
        ))

    # ── Simulate Timing Paths ─────────────────────────────────────────────────
    timing_paths: List[TimingPath] = []
    rng = random.Random(42)

    ENDPOINTS = [
        ("reg_alu_q7",    "reg_out_d7"),
        ("reg_pc_q15",    "reg_npc_d15"),
        ("mem_ctrl_addr7","sram_a7"),
        ("fifo_wr_ptr3",  "fifo_rd_ptr3"),
        ("div_unit_rem15","reg_result_d15"),
        ("mul_unit_p31",  "acc_d31"),
        ("dec_opcode",    "exe_ctrl_sel"),
        ("fpu_add_sum",   "fpu_norm_in"),
        ("cache_tag_hit", "arb_req"),
        ("bus_ctrl_grant","master_ack"),
    ]

    ref_period = clocks[0].period_ns if clocks else 2.5
    unc = uncertainties[0]['setup'] if uncertainties else 0.0

    wns = 999.0
    tns = 0.0

    for sp, ep in ENDPOINTS:
        slack = rng.uniform(-0.28, 0.42)
        arrival = ref_period - slack - unc
        required = ref_period - unc
        tp = TimingPath(
            startpoint=sp, endpoint=ep,
            clock=clocks[0].name if clocks else "clk",
            arrival_time_ns=round(arrival, 3),
            required_time_ns=round(required, 3),
            slack_ns=round(slack, 3),
        )
        timing_paths.append(tp)
        if slack < wns: wns = slack
        if slack < 0:   tns += slack

    timing_met = (wns >= 0.0)
    if wns < 0:
        violations.append(new_violation(
            ViolationType.NEGATIVE_SLACK, Severity.ERROR,
            f"WNS = {wns:.3f} ns — setup timing VIOLATED.",
            "Options: (1) upsize drivers (2) insert buffers (3) retime registers (4) reduce clock freq",
            constraint_id="STA_001", value=wns
        ))
    if tns < -1.0:
        violations.append(new_violation(
            ViolationType.TNS_VIOLATION, Severity.ERROR,
            f"TNS = {tns:.3f} ns — multiple failing paths.",
            "Run ECO flow or re-synthesize with tighter SDC constraints.",
            constraint_id="STA_002", value=tns
        ))

    # ── MMMC Corners ──────────────────────────────────────────────────────────
    mmmc_corners: List[MMMCCorner] = []
    if enable_mmmc and clocks:
        corners_def = [
            ("ss_0p72v_125c", "ss", 0.72, 125, +0.18),  # slow slow — worst setup
            ("tt_0p80v_025c", "tt", 0.80,  25,  0.00),  # typical
            ("ff_0p88v_m40c", "ff", 0.88, -40, -0.12),  # fast fast — worst hold
        ]
        for name, proc, volt, temp, derate in corners_def:
            c_wns = round(wns + derate, 3)
            c_tns = round(tns + (derate * 4 if derate < 0 else 0), 3)
            mmmc_corners.append(MMMCCorner(
                name=name, process=proc, voltage=volt, temperature=temp,
                wns_ns=c_wns, tns_ns=c_tns, timing_met=(c_wns >= 0.0)
            ))

    # ── Coverage Score ────────────────────────────────────────────────────────
    score = 0.0
    if clocks:             score += 25
    if input_delays:       score += 20
    if output_delays:      score += 20
    if uncertainties:      score += 15
    if false_paths or multicycle_paths: score += 10
    if len(violations) == 0: score += 10

    err_count  = sum(1 for v in violations if v.severity == Severity.ERROR)
    warn_count = sum(1 for v in violations if v.severity == Severity.WARNING)
    info_count = sum(1 for v in violations if v.severity == Severity.INFO)

    return SDCAnalysisResult(
        analysis_id   = str(uuid.uuid4())[:8],
        filename      = filename,
        total_lines   = len(lines),
        clocks        = clocks,
        input_delay_count  = len(input_delays),
        output_delay_count = len(output_delays),
        false_path_count   = len(false_paths),
        multicycle_path_count = len(multicycle_paths),
        uncertainty_count  = len(uncertainties),
        violations    = violations,
        timing_paths  = timing_paths,
        mmmc_corners  = mmmc_corners,
        error_count   = err_count,
        warning_count = warn_count,
        info_count    = info_count,
        worst_slack_ns= round(wns if wns < 999 else 0.0, 3),
        total_negative_slack_ns = round(tns, 3),
        timing_met    = timing_met,
        coverage_score= round(score, 1),
        analyzed_at   = datetime.utcnow().isoformat(),
    )

# ─── Routes ──────────────────────────────────────────────────────────────────

@app.get("/")
def root():
    return {"service": "TimingGuard SDC Validator", "version": "1.4.0"}

@app.get("/api/health")
def health():
    return {"status": "ok", "timestamp": datetime.utcnow().isoformat()}

@app.post("/api/analyze", response_model=SDCAnalysisResult)
def analyze(req: AnalyzeRequest):
    return parse_sdc(req.sdc_content, req.filename, req.enable_mmmc, req.design_name)

@app.post("/api/analyze/upload", response_model=SDCAnalysisResult)
async def analyze_upload(file: UploadFile = File(...), enable_mmmc: bool = True):
    content = (await file.read()).decode("utf-8")
    return parse_sdc(content, file.filename or "upload.sdc", enable_mmmc, "design")

@app.get("/api/sample-sdcs")
def sample_sdcs():
    return {
        "good": SAMPLE_GOOD_SDC,
        "bad":  SAMPLE_BAD_SDC,
        "mmmc": SAMPLE_MMMC_SDC,
    }

# ─── Sample SDC Files ─────────────────────────────────────────────────────────

SAMPLE_GOOD_SDC = """\
# TimingGuard Sample — Well-constrained SDC
# Design: riscv_core | PDK: sky130 | Freq: 400MHz

# ── Clocks ──────────────────────────────────────────────────────────────────
create_clock -name clk      -period 2.5  [get_ports clk]
create_clock -name clk_fast -period 1.25 [get_ports clk_fast]
create_generated_clock -name clk_div2 -master_clock clk -divide_by 2 [get_pins clk_div/Q]

# ── Clock Uncertainty ───────────────────────────────────────────────────────
set_clock_uncertainty -setup 0.100 -hold 0.050 [all_clocks]
set_clock_transition   0.08 [all_clocks]
set_clock_latency -source 0.12 [all_clocks]

# ── I/O Delays ──────────────────────────────────────────────────────────────
set_input_delay  -max 0.8 -clock clk [all_inputs]
set_input_delay  -min 0.2 -clock clk [all_inputs]
set_output_delay -max 0.8 -clock clk [all_outputs]
set_output_delay -min 0.1 -clock clk [all_outputs]

# ── CDC Exceptions ──────────────────────────────────────────────────────────
set_false_path -from [get_clocks clk] -to [get_clocks clk_fast]
set_false_path -from [get_clocks clk_fast] -to [get_clocks clk]

# ── Multicycle Paths ────────────────────────────────────────────────────────
set_multicycle_path -setup 2 -from [get_cells div_unit/*] -to [get_cells acc/*]
set_multicycle_path -hold  1 -from [get_cells div_unit/*] -to [get_cells acc/*]

# ── Load / Drive ─────────────────────────────────────────────────────────────
set_load 0.02 [all_outputs]
set_driving_cell -lib_cell sky130_fd_sc_hd__buf_2 [all_inputs]
"""

SAMPLE_BAD_SDC = """\
# TimingGuard Sample — Poorly constrained SDC (has violations)
# Missing: uncertainty, hold MCP, CDC exceptions

create_clock -name clk -period 0.3 [get_ports clk]

# I/O delays referencing different clocks (CDC unhandled)
set_input_delay  -max 1.8 -clock clk [all_inputs]
set_output_delay -max 2.1 -clock clk [all_outputs]

# MCP setup without hold compensation
set_multicycle_path -setup 3 -from [get_cells slow_path/*]
"""

SAMPLE_MMMC_SDC = """\
# TimingGuard Sample — MMMC SDC
# Three corners: ss/tt/ff

create_clock -name clk -period 2.5 [get_ports clk]
set_clock_uncertainty -setup 0.12 -hold 0.06 [all_clocks]
set_clock_transition 0.09 [all_clocks]

set_input_delay  -max 0.9 -clock clk [all_inputs]
set_input_delay  -min 0.2 -clock clk [all_inputs]
set_output_delay -max 0.9 -clock clk [all_outputs]
set_output_delay -min 0.1 -clock clk [all_outputs]

# MMMC timing derates
set_timing_derate -cell_delay -data      1.05 -late
set_timing_derate -cell_delay -data      0.97 -early
set_timing_derate -net_delay             1.03 -late
"""
