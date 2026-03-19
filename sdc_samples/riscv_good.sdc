# ── SAMPLE 1: Good SDC (riscv_core, sky130, 400MHz) ──────────────────────────
# File: samples/riscv_good.sdc

create_clock -name clk      -period 2.5  [get_ports clk]
create_clock -name clk_fast -period 1.25 [get_ports clk_fast]
create_generated_clock -name clk_div2 -master_clock clk -divide_by 2 \
    [get_pins u_clkdiv/Q]

set_clock_uncertainty -setup 0.100 -hold 0.050 [all_clocks]
set_clock_transition   0.080 [all_clocks]
set_clock_latency -source 0.120 [all_clocks]

set_input_delay  -max 0.80 -clock clk [all_inputs]
set_input_delay  -min 0.20 -clock clk [all_inputs]
set_output_delay -max 0.80 -clock clk [all_outputs]
set_output_delay -min 0.10 -clock clk [all_outputs]

set_false_path -from [get_clocks clk]      -to [get_clocks clk_fast]
set_false_path -from [get_clocks clk_fast] -to [get_clocks clk]
set_false_path -from [get_ports rst_n]

set_multicycle_path -setup 2 -from [get_cells u_div/*]  -to [get_cells u_acc/*]
set_multicycle_path -hold  1 -from [get_cells u_div/*]  -to [get_cells u_acc/*]
set_multicycle_path -setup 3 -from [get_cells u_fpu/*]  -to [get_cells u_wb/*]
set_multicycle_path -hold  2 -from [get_cells u_fpu/*]  -to [get_cells u_wb/*]

set_load          0.020 [all_outputs]
set_driving_cell  -lib_cell sky130_fd_sc_hd__buf_2 [all_inputs]
set_max_fanout    20 [all_inputs]
