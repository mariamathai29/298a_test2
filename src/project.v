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

    // Pin mapping:
    //
    // ui_in[7:0] = data_in
    // uio_in[0]  = load
    // uio_in[1]  = output enable
    // rst_n      = Tiny Tapeout active-low reset
    // clk        = counter clock
    //
    // uo_out[7:0] = counter output

    wire [7:0] count;

    counter counter_inst (
        .load    (uio_in[0]),
        .rst     (~rst_n),
        .data_in (ui_in),
        .clk     (clk),
        .oe      (uio_in[1]),
        .count   (count)
    );

    assign uo_out = count;

    // Bidirectional outputs aren't being used
    assign uio_out = 8'b0;
    assign uio_oe  = 8'b0;

    // Prevent unused-input warnings
    wire _unused = &{ena, uio_in[7:2], 1'b0};

endmodule


module counter (
    input  wire       load,
    input  wire       rst,
    input  wire [7:0] data_in,
    input  wire       clk,
    input  wire       oe,
    output wire [7:0] count
);

    logic [7:0] count_r;

    always_ff @(posedge clk or posedge rst) begin
        if (rst) begin
            count_r <= 8'd0;
        end else begin
            if (load) begin
                count_r <= data_in;
            end else begin
                count_r <= count_r + 8'd1;
            end
        end
    end

    assign count = oe ? count_r : 8'bz;

endmodule

`default_nettype wire
