/*
8 bit SPI modules
*/

module spi_transmit (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        load,
    input  wire [7:0] d_a,
    input  wire [7:0] d_b,
    input  wire [7:0] d_c,
    output reg         sclk_o,
    output reg         fs_o,
    output wire [2:0]  sdo          // {c, b, a}
);

  reg [7:0] d_a_hold;
  reg [7:0] d_b_hold;
  reg [7:0] d_c_hold;
  reg [2:0] bit_counter;
  reg transmitting;

  // Continuous assignments for serial outputs
  assign sdo[0] = d_a_hold[7];
  assign sdo[1] = d_b_hold[7];
  assign sdo[2] = d_c_hold[7];

  always @(posedge clk or negedge rst_n)
  begin
    if (!rst_n)
    begin
      fs_o          <= 1'h0;
      d_b_hold      <= 8'h00;
      d_a_hold      <= 8'h00;
      d_c_hold      <= 8'h00;
      transmitting  <= 1'h0;
      bit_counter   <= 3'h0;
      sclk_o        <= 1'h0;
    end
    else
    begin
      if (!transmitting)
      begin
        sclk_o <= 1'h0;
        if (load)
        begin
          d_a_hold     <= d_a;
          d_b_hold     <= d_b;
          d_c_hold     <= d_c;
          transmitting <= 1'h1;
          fs_o         <= 1'h1;
          bit_counter  <= 3'h0;
        end
      end
      else
      begin
        sclk_o <= ~sclk_o;

        if (sclk_o == 1'h1)
        begin
          fs_o <= 1'h0;
          if (bit_counter == 3'h7)
          begin
            transmitting <= 1'h0;
            bit_counter  <= 3'h0;
          end
          else
          begin
            d_a_hold    <= {d_a_hold[6:0], 1'h0};
            d_b_hold    <= {d_b_hold[6:0], 1'h0};
            d_c_hold    <= {d_c_hold[6:0], 1'h0};
            bit_counter <= bit_counter + 3'h1;
          end
        end
      end
    end
  end

endmodule
