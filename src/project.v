/*
 * Copyright (c) 2024 Your Name
 * SPDX-License-Identifier: Apache-2.0
 */

`default_nettype none

module counter (

    input load,
    input rst,
    input [7:0] data_in,
    input clk,

    output oe,
    output [7:0] count
);

logic [7:0] count_r;

always_ff @(posedge clk or posedge rst) begin
    if (rst) begin
        count_r <= 'd0;
    end else begin
        if (load) begin
            count_r <= data_in;
        end else begin
            count_r <= count+1;
        end
    end
end

assign count = oe ? count_r : 8'bz;

endmodule
