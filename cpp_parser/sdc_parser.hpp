#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <variant>
#include <memory>
#include <functional>

namespace timingguard {

// ─── SDC Constraint Types ────────────────────────────────────────────────────

enum class ConstraintType {
    CREATE_CLOCK,
    CREATE_GENERATED_CLOCK,
    SET_INPUT_DELAY,
    SET_OUTPUT_DELAY,
    SET_CLOCK_UNCERTAINTY,
    SET_CLOCK_LATENCY,
    SET_CLOCK_TRANSITION,
    SET_MULTICYCLE_PATH,
    SET_FALSE_PATH,
    SET_MAX_DELAY,
    SET_MIN_DELAY,
    SET_DRIVING_CELL,
    SET_LOAD,
    SET_TIMING_DERATE,
    SET_OPERATING_CONDITIONS,
    GROUP_PATH,
    UNKNOWN
};

enum class ViolationType {
    MISSING_CLOCK,
    UNCONSTRAINED_INPUT,
    UNCONSTRAINED_OUTPUT,
    OVERLY_TIGHT_CONSTRAINT,
    CONFLICTING_CONSTRAINTS,
    MISSING_UNCERTAINTY,
    MISSING_TRANSITION,
    INVALID_MULTICYCLE,
    CLOCK_DOMAIN_CROSSING,
    MMMC_CORNER_MISMATCH,
    NEGATIVE_SLACK,
    TNS_VIOLATION
};

enum class Severity { ERROR, WARNING, INFO };

struct ClockDef {
    std::string name;
    std::string source_port;
    double      period_ns;
    double      waveform_rise{0.0};
    double      waveform_fall{0.0};
    bool        is_virtual{false};
    bool        is_generated{false};
    std::string master_clock;
    double      divide_by{1.0};
    double      multiply_by{1.0};
    int         line_number{0};
};

struct InputDelay {
    std::string clock_name;
    double      delay_ns;
    std::string port_pattern;
    bool        is_max{true};
    bool        is_rise{true};
    int         line_number{0};
};

struct OutputDelay {
    std::string clock_name;
    double      delay_ns;
    std::string port_pattern;
    bool        is_max{true};
    int         line_number{0};
};

struct ClockUncertainty {
    std::string clock_name;
    double      setup_uncertainty_ns;
    double      hold_uncertainty_ns;
    int         line_number{0};
};

struct MulticyclePath {
    int         cycles;
    std::string from_clock;
    std::string to_clock;
    bool        is_setup{true};
    int         line_number{0};
};

struct FalsePath {
    std::string from;
    std::string to;
    std::string through;
    int         line_number{0};
};

struct MaxMinDelay {
    double      delay_ns;
    std::string from;
    std::string to;
    bool        is_max{true};
    int         line_number{0};
};

struct MMMCCorner {
    std::string name;
    std::string process;    // ff, tt, ss
    double      voltage;
    double      temperature;
    std::string library;
};

struct TimingPath {
    std::string startpoint;
    std::string endpoint;
    std::string clock;
    double      arrival_time_ns;
    double      required_time_ns;
    double      slack_ns;
    bool        is_setup{true};
    std::vector<std::string> path_cells;
};

struct Violation {
    ViolationType type;
    Severity      severity;
    std::string   message;
    std::string   suggestion;
    int           line_number{0};
    std::string   constraint_id;
    double        value{0.0};
};

struct SDCParseResult {
    std::vector<ClockDef>        clocks;
    std::vector<InputDelay>      input_delays;
    std::vector<OutputDelay>     output_delays;
    std::vector<ClockUncertainty> uncertainties;
    std::vector<MulticyclePath>  multicycle_paths;
    std::vector<FalsePath>       false_paths;
    std::vector<MaxMinDelay>     max_min_delays;
    std::vector<MMMCCorner>      mmmc_corners;
    std::vector<Violation>       violations;
    std::vector<TimingPath>      timing_paths;
    int                          total_lines{0};
    int                          error_count{0};
    int                          warning_count{0};
    double                       worst_slack_ns{0.0};
    double                       total_negative_slack_ns{0.0};
    bool                         timing_met{false};
};

// ─── SDC Parser ──────────────────────────────────────────────────────────────

class SDCParser {
public:
    SDCParseResult parse(const std::string& sdc_content);
    SDCParseResult parse_file(const std::string& filepath);

    // Validation passes
    void validate_clock_coverage(SDCParseResult& result);
    void validate_io_constraints(SDCParseResult& result);
    void validate_uncertainty(SDCParseResult& result);
    void validate_multicycle_paths(SDCParseResult& result);
    void validate_mmmc_corners(SDCParseResult& result, const std::vector<MMMCCorner>& corners);
    void check_cdc_paths(SDCParseResult& result);

    // STA simulation
    void simulate_timing(SDCParseResult& result);
    std::vector<TimingPath> compute_critical_paths(const SDCParseResult& result, int top_n = 10);

    // Fix suggestions
    std::string suggest_fix(const Violation& v, const SDCParseResult& ctx);

private:
    std::vector<std::string> tokenize(const std::string& line);
    ClockDef     parse_create_clock(const std::vector<std::string>& tokens, int line_no);
    InputDelay   parse_input_delay(const std::vector<std::string>& tokens, int line_no);
    OutputDelay  parse_output_delay(const std::vector<std::string>& tokens, int line_no);
    MulticyclePath parse_multicycle(const std::vector<std::string>& tokens, int line_no);
    FalsePath    parse_false_path(const std::vector<std::string>& tokens, int line_no);

    double extract_number(const std::vector<std::string>& tokens, const std::string& flag, double def = 0.0);
    std::string extract_string(const std::vector<std::string>& tokens, const std::string& flag, const std::string& def = "");
    bool has_flag(const std::vector<std::string>& tokens, const std::string& flag);
};

// ─── MMMC Manager ────────────────────────────────────────────────────────────

class MMMCAnalyzer {
public:
    void add_corner(const MMMCCorner& corner);
    void set_base_constraints(const SDCParseResult& base);

    struct CornerResult {
        MMMCCorner corner;
        SDCParseResult result;
        double wns_ns;
        double tns_ns;
        bool   timing_met;
    };

    std::vector<CornerResult> analyze_all_corners();
    CornerResult              worst_corner() const;
    std::vector<Violation>    cross_corner_violations() const;

private:
    std::vector<MMMCCorner>  corners_;
    SDCParseResult           base_constraints_;
    std::vector<CornerResult> corner_results_;
};

} // namespace timingguard
