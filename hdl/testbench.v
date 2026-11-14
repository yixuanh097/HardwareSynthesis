`timescale 1ns/1ps
module testbench;
    reg clk = 0, rst = 1, step = 0, ld = 0;
    reg [3:0] ld_sel = 0;
    reg [7:0] ld_val = 0;
    reg [4:0] fu_sel = 0;
    wire [4:0] cyc;
    wire [7:0] fu_out;
    wire fu_busy;
    integer errors = 0;
    hls_core dut (.clk(clk), .rst(rst), .step(step), .ld(ld), .ld_sel(ld_sel), .ld_val(ld_val),
                  .fu_sel(fu_sel), .cyc(cyc), .fu_out(fu_out), .fu_busy(fu_busy));
    always #10 clk = ~clk;

    task check(input [4:0] unit, input [7:0] value);
        begin
            fu_sel = unit;
            #1 if(fu_out !== value || fu_busy !== 1) begin
                errors = errors + 1;
                $display("FAIL cycle %0d unit %0d: got %0d (busy %b), expected %0d", cyc, unit, fu_out, fu_busy, value);
            end
        end
    endtask

    // a unit with nothing to compute this cycle, or a number with no unit, shows -- on the board
    task idle(input [4:0] unit);
        begin
            fu_sel = unit;
            #1 if(fu_busy !== 0) begin
                errors = errors + 1;
                $display("FAIL cycle %0d unit %0d: busy, expected idle", cyc, unit);
            end
        end
    endtask

    task load(input [3:0] slot, input [7:0] value);
        begin
            ld = 1; ld_sel = slot; ld_val = value;
            @(posedge clk) #1 ld = 0;
        end
    endtask

    task do_step;
        begin
            step = 1;
            @(posedge clk) #1 step = 0;
        end
    endtask

    initial begin
        // inputs: A0=2 B0=3
        rst = 1;
        @(posedge clk) #1 rst = 0;
        check(0, 5); // cycle 0: X0
        idle(1); idle(2);
        do_step;
        check(0, 7); // cycle 1: Y0
        check(1, 2); // cycle 1: Z0
        idle(2);
        do_step;
        check(2, 6); // cycle 2: Q0
        idle(0); idle(1); idle(3); // unit 3 does not exist (SW1 and SW0 up)
        do_step;
        idle(0); idle(1); idle(2); // cycle 3: done
        // inputs: A0=37 B0=43
        rst = 1;
        @(posedge clk) #1 rst = 0;
        load(0, 37);
        load(1, 43);
        check(0, 80); // cycle 0: X0
        do_step;
        check(0, 117); // cycle 1: Y0
        check(1, 37); // cycle 1: Z0
        do_step;
        check(2, 55); // cycle 2: Q0
        do_step;
        // inputs: A0=12 B0=8
        rst = 1;
        @(posedge clk) #1 rst = 0;
        load(0, 12);
        load(1, 8);
        check(0, 20); // cycle 0: X0
        do_step;
        check(0, 32); // cycle 1: Y0
        check(1, 12); // cycle 1: Z0
        do_step;
        check(2, 96); // cycle 2: Q0
        do_step;
        if (errors == 0) $display("PASS");
        else $display("FAIL: %0d errors", errors);
        $finish;
    end
endmodule
