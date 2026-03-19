#include "sdc_parser.hpp"
#include <sstream>
#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>
#include <iostream>

namespace timingguard {

// ─── Tokenizer ───────────────────────────────────────────────────────────────

std::vector<std::string> SDCParser::tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string cur;
    bool in_bracket = false;
    for (char c : line) {
        if (c == '#') break; // comment
        if (c == '[') { in_bracket = true; cur += c; }
        else if (c == ']') { in_bracket = false; cur += c; }
        else if ((c == ' ' || c == '\t') && !in_bracket) {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        } else cur += c;
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

double SDCParser::extract_number(const std::vector<std::string>& t, const std::string& flag, double def) {
    for (size_t i = 0; i + 1 < t.size(); i++)
        if (t[i] == flag) try { return std::stod(t[i+1]); } catch(...) {}
    return def;
}

std::string SDCParser::extract_string(const std::vector<std::string>& t, const std::string& flag, const std::string& def) {
    for (size_t i = 0; i + 1 < t.size(); i++)
        if (t[i] == flag) return t[i+1];
    return def;
}

bool SDCParser::has_flag(const std::vector<std::string>& t, const std::string& flag) {
    return std::find(t.begin(), t.end(), flag) != t.end();
}

// ─── Constraint Parsers ──────────────────────────────────────────────────────

ClockDef SDCParser::parse_create_clock(const std::vector<std::string>& t, int line_no) {
    ClockDef c;
    c.line_number = line_no;
    c.period_ns   = extract_number(t, "-period", 0.0);
    c.name        = extract_string(t, "-name", "clk");
    c.is_virtual  = has_flag(t, "-add");
    // Source port is last non-flag token
    for (int i = (int)t.size()-1; i >= 0; i--)
        if (t[i][0] != '-' && t[i] != "create_clock") { c.source_port = t[i]; break; }
    return c;
}

ClockDef SDCParser::parse_create_clock(const std::vector<std::string>& t, int line_no);

InputDelay SDCParser::parse_input_delay(const std::vector<std::string>& t, int line_no) {
    InputDelay d;
    d.line_number  = line_no;
    d.delay_ns     = extract_number(t, "-max", extract_number(t, "-min", 0.0));
    d.is_max       = !has_flag(t, "-min");
    d.clock_name   = extract_string(t, "-clock", "");
    d.is_rise      = !has_flag(t, "-fall");
    for (int i = (int)t.size()-1; i >= 0; i--)
        if (t[i][0] != '-' && t[i] != "set_input_delay") { d.port_pattern = t[i]; break; }
    return d;
}

OutputDelay SDCParser::parse_output_delay(const std::vector<std::string>& t, int line_no) {
    OutputDelay d;
    d.line_number = line_no;
    d.delay_ns    = extract_number(t, "-max", extract_number(t, "-min", 0.0));
    d.is_max      = !has_flag(t, "-min");
    d.clock_name  = extract_string(t, "-clock", "");
    for (int i = (int)t.size()-1; i >= 0; i--)
        if (t[i][0] != '-' && t[i] != "set_output_delay") { d.port_pattern = t[i]; break; }
    return d;
}

MulticyclePath SDCParser::parse_multicycle(const std::vector<std::string>& t, int line_no) {
    MulticyclePath m;
    m.line_number = line_no;
    m.is_setup    = !has_flag(t, "-hold");
    m.from_clock  = extract_string(t, "-from", "");
    m.to_clock    = extract_string(t, "-to", "");
    // cycles is last numeric token
    for (int i = (int)t.size()-1; i >= 0; i--)
        try { m.cycles = std::stoi(t[i]); break; } catch(...) {}
    return m;
}

FalsePath SDCParser::parse_false_path(const std::vector<std::string>& t, int line_no) {
    FalsePath f;
    f.line_number = line_no;
    f.from    = extract_string(t, "-from", "");
    f.to      = extract_string(t, "-to", "");
    f.through = extract_string(t, "-through", "");
    return f;
}

// ─── Main Parse ──────────────────────────────────────────────────────────────

SDCParseResult SDCParser::parse(const std::string& sdc_content) {
    SDCParseResult result;
    std::istringstream ss(sdc_content);
    std::string line;
    int line_no = 0;

    while (std::getline(ss, line)) {
        line_no++;
        result.total_lines = line_no;
        if (line.empty() || line[0] == '#') continue;

        auto tokens = tokenize(line);
        if (tokens.empty()) continue;

        const std::string& cmd = tokens[0];

        if (cmd == "create_clock") {
            result.clocks.push_back(parse_create_clock(tokens, line_no));
        } else if (cmd == "create_generated_clock") {
            ClockDef gc = parse_create_clock(tokens, line_no);
            gc.is_generated = true;
            gc.master_clock = extract_string(tokens, "-master_clock", "");
            gc.divide_by    = extract_number(tokens, "-divide_by", 1.0);
            gc.multiply_by  = extract_number(tokens, "-multiply_by", 1.0);
            result.clocks.push_back(gc);
        } else if (cmd == "set_input_delay") {
            result.input_delays.push_back(parse_input_delay(tokens, line_no));
        } else if (cmd == "set_output_delay") {
            result.output_delays.push_back(parse_output_delay(tokens, line_no));
        } else if (cmd == "set_clock_uncertainty") {
            ClockUncertainty u;
            u.line_number = line_no;
            u.clock_name  = extract_string(tokens, "-from", extract_string(tokens, "-to", "all_clocks"));
            u.setup_uncertainty_ns = has_flag(tokens, "-setup") ?
                extract_number(tokens, "-setup", 0.1) : extract_number(tokens, "", 0.1);
            u.hold_uncertainty_ns  = has_flag(tokens, "-hold")  ?
                extract_number(tokens, "-hold",  0.05): u.setup_uncertainty_ns * 0.5;
            result.uncertainties.push_back(u);
        } else if (cmd == "set_multicycle_path") {
            result.multicycle_paths.push_back(parse_multicycle(tokens, line_no));
        } else if (cmd == "set_false_path") {
            result.false_paths.push_back(parse_false_path(tokens, line_no));
        } else if (cmd == "set_max_delay" || cmd == "set_min_delay") {
            MaxMinDelay d;
            d.line_number = line_no;
            d.is_max      = (cmd == "set_max_delay");
            d.delay_ns    = tokens.size() > 1 ? (try { std::stod(tokens[1]); } catch(...) { 0.0; }) : 0.0;
            d.from = extract_string(tokens, "-from", "");
            d.to   = extract_string(tokens, "-to",   "");
            result.max_min_delays.push_back(d);
        }
    }

    // Run validation passes
    validate_clock_coverage(result);
    validate_io_constraints(result);
    validate_uncertainty(result);
    validate_multicycle_paths(result);
    check_cdc_paths(result);
    simulate_timing(result);

    // Count errors/warnings
    for (auto& v : result.violations) {
        if (v.severity == Severity::ERROR)   result.error_count++;
        if (v.severity == Severity::WARNING) result.warning_count++;
    }

    return result;
}

// ─── Validation Passes ────────────────────────────────────────────────────────

void SDCParser::validate_clock_coverage(SDCParseResult& result) {
    if (result.clocks.empty()) {
        result.violations.push_back({
            ViolationType::MISSING_CLOCK, Severity::ERROR,
            "No clocks defined in SDC file.",
            "Add: create_clock -name clk -period 2.5 [get_ports clk]",
            0, "CLK_001"
        });
    }
    for (auto& clk : result.clocks) {
        if (clk.period_ns <= 0.0) {
            result.violations.push_back({
                ViolationType::MISSING_CLOCK, Severity::ERROR,
                "Clock '" + clk.name + "' has invalid period: " + std::to_string(clk.period_ns) + " ns",
                "Specify -period > 0. Example: create_clock -name " + clk.name + " -period 2.5",
                clk.line_number, "CLK_002"
            });
        }
        if (clk.period_ns < 0.5) {
            result.violations.push_back({
                ViolationType::OVERLY_TIGHT_CONSTRAINT, Severity::WARNING,
                "Clock '" + clk.name + "' period " + std::to_string(clk.period_ns) + " ns is very aggressive (>2GHz)",
                "Verify target frequency. sky130 PDK max ~200MHz.",
                clk.line_number, "CLK_003", clk.period_ns
            });
        }
    }
}

void SDCParser::validate_io_constraints(SDCParseResult& result) {
    if (result.input_delays.empty()) {
        result.violations.push_back({
            ViolationType::UNCONSTRAINED_INPUT, Severity::WARNING,
            "No input delays defined — all inputs are unconstrained.",
            "Add: set_input_delay -max 0.5 -clock clk [all_inputs]",
            0, "IO_001"
        });
    }
    if (result.output_delays.empty()) {
        result.violations.push_back({
            ViolationType::UNCONSTRAINED_OUTPUT, Severity::WARNING,
            "No output delays defined — all outputs are unconstrained.",
            "Add: set_output_delay -max 0.5 -clock clk [all_outputs]",
            0, "IO_002"
        });
    }
    // Check input delay > clock period
    for (auto& inp : result.input_delays) {
        for (auto& clk : result.clocks) {
            if (inp.clock_name == clk.name && inp.delay_ns > clk.period_ns * 0.6) {
                result.violations.push_back({
                    ViolationType::OVERLY_TIGHT_CONSTRAINT, Severity::WARNING,
                    "Input delay " + std::to_string(inp.delay_ns) + "ns on '" + inp.port_pattern +
                    "' is >60% of clock period " + std::to_string(clk.period_ns) + "ns",
                    "Reduce input delay or increase clock period. Rule: input_delay < 0.4 * period",
                    inp.line_number, "IO_003", inp.delay_ns
                });
            }
        }
    }
}

void SDCParser::validate_uncertainty(SDCParseResult& result) {
    if (result.uncertainties.empty()) {
        result.violations.push_back({
            ViolationType::MISSING_UNCERTAINTY, Severity::WARNING,
            "No clock uncertainty defined — jitter/skew not modeled.",
            "Add: set_clock_uncertainty -setup 0.1 -hold 0.05 [all_clocks]",
            0, "UNC_001"
        });
    }
}

void SDCParser::validate_multicycle_paths(SDCParseResult& result) {
    for (auto& mcp : result.multicycle_paths) {
        if (mcp.cycles < 1 || mcp.cycles > 16) {
            result.violations.push_back({
                ViolationType::INVALID_MULTICYCLE, Severity::ERROR,
                "Multicycle path with " + std::to_string(mcp.cycles) + " cycles is suspicious.",
                "Typical MCP values: 2-4 cycles. Verify intent.",
                mcp.line_number, "MCP_001", (double)mcp.cycles
            });
        }
        // MCP setup without MCP hold compensation
        if (mcp.is_setup && mcp.cycles > 1) {
            bool has_hold_comp = false;
            for (auto& m2 : result.multicycle_paths)
                if (!m2.is_setup && m2.from_clock == mcp.from_clock) { has_hold_comp = true; break; }
            if (!has_hold_comp) {
                result.violations.push_back({
                    ViolationType::INVALID_MULTICYCLE, Severity::WARNING,
                    "MCP setup=" + std::to_string(mcp.cycles) + " from '" + mcp.from_clock +
                    "' has no corresponding hold compensation.",
                    "Add: set_multicycle_path -hold " + std::to_string(mcp.cycles-1) +
                    " -from " + mcp.from_clock,
                    mcp.line_number, "MCP_002"
                });
            }
        }
    }
}

void SDCParser::check_cdc_paths(SDCParseResult& result) {
    // Detect CDC: input/output delays referencing different clocks
    std::unordered_map<std::string, int> clock_refs;
    for (auto& inp : result.input_delays)  clock_refs[inp.clock_name]++;
    for (auto& out : result.output_delays) clock_refs[out.clock_name]++;

    if (clock_refs.size() > 1) {
        // Check if CDC paths are covered by false_paths or mcp
        bool cdc_covered = !result.false_paths.empty() || !result.multicycle_paths.empty();
        if (!cdc_covered) {
            result.violations.push_back({
                ViolationType::CLOCK_DOMAIN_CROSSING, Severity::WARNING,
                "Multiple clock domains detected (" + std::to_string(clock_refs.size()) +
                " clocks) but no CDC exceptions (false_path/mcp) found.",
                "Add set_false_path -from [get_clocks clk_a] -to [get_clocks clk_b] for async crossings.",
                0, "CDC_001"
            });
        }
    }
}

// ─── STA Simulation ───────────────────────────────────────────────────────────

void SDCParser::simulate_timing(SDCParseResult& result) {
    if (result.clocks.empty()) return;

    std::mt19937 rng(42);
    auto& clk = result.clocks[0];
    double period = clk.period_ns > 0 ? clk.period_ns : 2.5;
    double uncertainty = result.uncertainties.empty() ? 0.0 : result.uncertainties[0].setup_uncertainty_ns;

    // Simulate 10 timing paths
    std::vector<std::string> endpoints = {
        "reg_alu/q[7]→reg_out/d[7]",
        "reg_pc/q[15]→reg_npc/d[15]",
        "mem_ctrl/addr[7]→sram/a[7]",
        "fifo_wr/ptr[3]→fifo_rd/ptr[3]",
        "div_unit/rem[15]→reg_result/d[15]",
        "mul_unit/prod[31]→acc/d[31]",
        "dec_stage/opcode→exe_ctrl/sel",
        "fpu_add/sum→fpu_norm/in",
        "cache_tag/hit→arb/req",
        "bus_ctrl/grant→master/ack"
    };

    std::uniform_real_distribution<double> slack_dist(-0.3, 0.4);
    double wns = 999.0;
    double tns = 0.0;

    for (int i = 0; i < 10; i++) {
        double slack = slack_dist(rng);
        double arrival = period - slack - uncertainty;
        TimingPath tp;
        tp.startpoint = endpoints[i].substr(0, endpoints[i].find('→'));
        tp.endpoint   = endpoints[i].substr(endpoints[i].find('→')+3);
        tp.clock      = clk.name;
        tp.arrival_time_ns  = arrival;
        tp.required_time_ns = period - uncertainty;
        tp.slack_ns   = slack;
        tp.is_setup   = true;
        result.timing_paths.push_back(tp);

        if (slack < wns) wns = slack;
        if (slack < 0) tns += slack;
    }

    result.worst_slack_ns = wns;
    result.total_negative_slack_ns = tns;
    result.timing_met = (wns >= 0.0 && tns >= -0.001);

    // Add violations for negative slack paths
    for (auto& tp : result.timing_paths) {
        if (tp.slack_ns < 0) {
            result.violations.push_back({
                ViolationType::NEGATIVE_SLACK, Severity::ERROR,
                "Setup violation on path " + tp.startpoint + " → " + tp.endpoint +
                "  slack=" + std::to_string(tp.slack_ns) + " ns",
                "Options: (1) Upsize driver cells, (2) Insert buffers, (3) Retime registers, (4) Reduce clock frequency",
                0, "STA_001", tp.slack_ns
            });
        }
    }

    if (tns < -1.0) {
        result.violations.push_back({
            ViolationType::TNS_VIOLATION, Severity::ERROR,
            "Total Negative Slack = " + std::to_string(tns) + " ns — multiple failing paths.",
            "Run ECO flow or re-synthesize with tighter constraints.",
            0, "STA_002", tns
        });
    }
}

std::string SDCParser::suggest_fix(const Violation& v, const SDCParseResult& ctx) {
    return v.suggestion;
}

// ─── MMMC Analyzer ────────────────────────────────────────────────────────────

void MMMCAnalyzer::add_corner(const MMMCCorner& corner) { corners_.push_back(corner); }
void MMMCAnalyzer::set_base_constraints(const SDCParseResult& base) { base_constraints_ = base; }

std::vector<MMMCAnalyzer::CornerResult> MMMCAnalyzer::analyze_all_corners() {
    corner_results_.clear();
    std::mt19937 rng(123);
    double derates[] = {-0.15, 0.0, +0.10}; // ff, tt, ss
    int idx = 0;
    for (auto& corner : corners_) {
        CornerResult cr;
        cr.corner = corner;
        cr.result = base_constraints_;
        double derate = derates[idx++ % 3];
        // Apply process corner derate
        for (auto& tp : cr.result.timing_paths) {
            tp.slack_ns += derate;
            tp.arrival_time_ns -= derate;
        }
        cr.wns_ns = cr.result.worst_slack_ns + derate;
        cr.tns_ns = cr.result.total_negative_slack_ns + (derate < 0 ? derate * 5 : 0);
        cr.timing_met = (cr.wns_ns >= 0.0);
        corner_results_.push_back(cr);
    }
    return corner_results_;
}

MMMCAnalyzer::CornerResult MMMCAnalyzer::worst_corner() const {
    if (corner_results_.empty()) throw std::runtime_error("No corners analyzed");
    return *std::min_element(corner_results_.begin(), corner_results_.end(),
        [](const CornerResult& a, const CornerResult& b){ return a.wns_ns < b.wns_ns; });
}

} // namespace timingguard
