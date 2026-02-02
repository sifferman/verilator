// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed under the Creative Commons Public Domain.
// SPDX-FileCopyrightText: 2026 Ethan Sifferman
// SPDX-License-Identifier: CC0-1.0

// Test that mixing whole-variable and partial NBAs with suspend points between
// them produces E_UNSUPPORTED error (not yet correctly supported)
//
// The "last NBA wins" semantics are not correctly implemented when there are
// suspend points between NBAs to the same variable with mixed whole/partial updates.

module t;

  int delay = 0; always #1 delay = int'($time) / 4;
  task automatic do_delay;
    if (delay > 0) #(delay);
  endtask

  // Test: whole then partial with suspend between
  logic [7:0] a = 0;
  always begin
    a <= 8'hff;
    do_delay();
    a[3:0] <= 4'h0; // Last NBA should win
    #1;
    // Expected: a = 8'hf0, Receives a = 8'h00
    assert (a == 8'hf0) else $fatal(0, "received %0h", a);
  end

  // Test: partial then whole with suspend between
  logic [7:0] b = 0;
  always begin
    b[3:0] <= 4'h0;
    do_delay();
    b <= 8'hff; // Last NBA should win
    #1;
    // Expected: b = 8'hff, Receives b = 8'h00
    assert (b == 8'hff) else $fatal(0, "received %0h", b);
  end

  initial begin
    #20;
    $write("*-* All Finished *-*\n");
    $finish;
  end

endmodule
