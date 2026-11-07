`default_nettype none

// Control flip-flop that synthesis must not merge with its copies.
//
// The ring controls each fan out to ~90 register bits across the tile. To
// keep every copy local to its loads they are replicated, and Yosys would
// merge identical inferred flops back into one, so the GF180 cell is
// instantiated directly (cells are never merged). Define AF_BEHAVIORAL_FF for
// tools without the PDK cell models.
module ctl_ff (
    input  wire clk,
    input  wire rst_n,
    input  wire d,
    output wire q
);
`ifdef AF_BEHAVIORAL_FF
    reg r;
    always @(posedge clk or negedge rst_n)
        if (!rst_n) r <= 1'b0;
        else        r <= d;
    assign q = r;
`else
    (* keep *) gf180mcu_fd_sc_mcu7t5v0__dffrnq_2 u_ff (
        .CLK (clk),
        .RN  (rst_n),
        .D   (d),
        .Q   (q)
    );
`endif
endmodule
