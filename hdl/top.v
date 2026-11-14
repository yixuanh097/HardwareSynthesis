// Terasic C5G top level
//   KEY[0] step one control step         KEY[1] load SW[5:0] into input SW[9:6]
//   SW[4:0] unit shown on HEX1-HEX0      CPU_RESET_n restart with the hardcoded inputs
//   HEX3-HEX2 control step, HEX1-HEX0 unit output (hex), -- when the unit computes nothing this step
module top (
    input CLOCK_50_B5B, CPU_RESET_n,
    input [1:0] KEY,
    input [9:0] SW,
    output [6:0] HEX0, HEX1, HEX2, HEX3
);
    // debounce: sample the keys and the reset button (active low) every 2^19 clocks, about 10 ms
    reg [18:0] t = 0;
    reg [1:0] k0 = 0, k1 = 0; // last two samples, 1 = pressed
    reg rst = 0;
    always @(posedge CLOCK_50_B5B) begin
        t <= t + 1;
        if (t == 0) begin
            k0 <= {k0[0], ~KEY[0]};
            k1 <= {k1[0], ~KEY[1]};
            rst <= ~CPU_RESET_n;
        end
    end

    wire [4:0] cyc;
    wire [7:0] fu_out;
    wire fu_busy;
    hls_core core (.clk(CLOCK_50_B5B), .rst(rst), .step(t == 1 && k0 == 2'b01), .ld(t == 1 && k1 == 2'b01),
                   .ld_sel(SW[9:6]), .ld_val({2'b00, SW[5:0]}), .fu_sel(SW[4:0]), .cyc(cyc), .fu_out(fu_out),
                   .fu_busy(fu_busy));

    // hex digit -> segments, active low, bit 0 = segment a
    function [6:0] seg(input [3:0] h);
        case (h)
            0: seg = 7'h40;  1: seg = 7'h79;  2: seg = 7'h24;  3: seg = 7'h30;
            4: seg = 7'h19;  5: seg = 7'h12;  6: seg = 7'h02;  7: seg = 7'h78;
            8: seg = 7'h00;  9: seg = 7'h10; 10: seg = 7'h08; 11: seg = 7'h03;
           12: seg = 7'h46; 13: seg = 7'h21; 14: seg = 7'h06; 15: seg = 7'h0E;
        endcase
    endfunction

    // -- (segment g only) when the selected unit computes nothing this step or does not exist
    assign HEX0 = fu_busy ? seg(fu_out[3:0]) : 7'h3F;
    assign HEX1 = fu_busy ? seg(fu_out[7:4]) : 7'h3F;
    assign HEX2 = seg(cyc[3:0]);
    assign HEX3 = seg({3'b000, cyc[4]});
endmodule
