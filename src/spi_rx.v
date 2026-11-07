/*
8 bit SPI modules
*/

module spi_receive(
    input  wire       sclk,
    input  wire       cs_n,
    input  wire       rst_n,
    input  wire       mosi_u,
    input  wire       mosi_r,
    output reg        d_tog,
    output reg  [7:0] d_hold_u,
    output reg  [7:0] d_hold_r
);

  reg [6:0] shift_u;
  reg [6:0] shift_r;
  reg [2:0] bit_counter;

  wire frame_rst = rst_n & ~cs_n;

  always @(posedge sclk or negedge frame_rst)
  begin
    if (!frame_rst)
      begin
        shift_u <= 7'h00;
        shift_r <= 7'h00;

        bit_counter <= 3'h0;
      end
    else
      begin
        shift_u <= {shift_u[5:0], mosi_u};
        shift_r <= {shift_r[5:0], mosi_r};

        bit_counter <= bit_counter + 3'h1;
      end
  end

  always @(posedge sclk or negedge rst_n)
  begin
    if (!rst_n)
      begin
        d_hold_u <= 8'h00;
        d_hold_r <= 8'h00;

        d_tog <= 1'h0;
      end
    else if (bit_counter == 3'h7)
      begin
        d_hold_u <= {shift_u, mosi_u};
        d_hold_r <= {shift_r, mosi_r};

        d_tog <= ~d_tog;
      end
  end

endmodule
