/*
 * Copyright (c) 2024 Your Name
 * SPDX-License-Identifier: Apache-2.0
 */


`default_nettype none

module tt_um_example (
    input  wire [7:0] ui_in,
    output wire [7:0] uo_out,
    input  wire [7:0] uio_in,
    output wire [7:0] uio_out,
    output wire [7:0] uio_oe,
    input  wire       ena,
    input  wire       clk,
    input  wire       rst_n
);

    // Tiny Tapeout pin mapping:
    // ui_in[0]   = synchronous load enable
    // ui_in[1]   = output enable for the bidirectional 8-bit bus
    // uio[7:0]   = data input while oe=0; counter output while oe=1
    // uo_out     = counter value (always visible for debugging/testing)
    // rst_n      = active-low asynchronous reset

    wire       load = ui_in[0];
    wire       oe   = ui_in[1];
    wire [7:0] count;

    counter counter_inst (
        .load    (load),
        .rst     (~rst_n),
        .data_in (uio_in),
        .clk     (clk),
        .count   (count)
    );

    // Dedicated outputs always expose the internal count.
    assign uo_out = count;

    // Implement the tri-state bus using Tiny Tapeout's explicit output-enable path.
    assign uio_out = count;
    assign uio_oe  = {8{oe}};

    // Prevent unused-input warnings.
    wire _unused = &{ena, ui_in[7:2], 1'b0};

endmodule

module counter (
    input  wire       load,
    input  wire       rst,
    input  wire [7:0] data_in,
    input  wire       clk,
    output wire [7:0] count
);

    logic [7:0] count_r;

    always_ff @(posedge clk or posedge rst) begin
        if (rst) begin
            count_r <= 8'd0;
        end else if (load) begin
            count_r <= data_in;
        end else begin
            count_r <= count_r + 8'd1;
        end
    end

    assign count = count_r;

endmodule

`default_nettype wire

