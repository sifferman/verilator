// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed under the Creative Commons Public Domain, for
// any use, without warranty, 2019 by Wilson Snyder.
// SPDX-License-Identifier: CC0-1.0

`define stop $stop
`define checkh(gotv,expv) do if ((gotv) !== (expv)) begin $write("%%Error: %s:%0d:  got='h%x exp='h%x\n", `__FILE__,`__LINE__, (gotv), (expv)); `stop; end while(0);
`define checkp(gotv,expv_s) do begin string gotv_s; gotv_s = $sformatf("%p", gotv); if ((gotv_s) != (expv_s)) begin $write("%%Error: %s:%0d:  got='%s' exp='%s'\n", `__FILE__,`__LINE__, (gotv_s), (expv_s)); `stop; end end while(0);

module t (/*AUTOARG*/);
   initial begin
      int q[5];
      int qv[$];  // Value returns
      int qi[$];  // Index returns
      int i;
      string v;

      q = '{1, 2, 2, 4, 3};
      `checkp(q, "'{'h1, 'h2, 'h2, 'h4, 'h3} ");

      // NOT tested: with ... selectors

      q.sort;
      `checkp(q, "'{'h1, 'h2, 'h2, 'h3, 'h4} ");
      q.sort with (item == 2);
      `checkp(q, "'{'h1, 'h3, 'h4, 'h2, 'h2} ");
      q.sort(x) with (x == 3);
      `checkp(q, "'{'h1, 'h4, 'h2, 'h2, 'h3} ");

      q.rsort;
      `checkp(q, "'{'h4, 'h3, 'h2, 'h2, 'h1} ");
      q.rsort with (item == 2);
      `checkp(q, "'{'h2, 'h2, 'h4, 'h3, 'h1} ");

      qv = q.unique;
      `checkp(qv, "'{'h2, 'h4, 'h3, 'h1} ");
      qi = q.unique_index; qi.sort;
      `checkp(qi, "'{'h0, 'h2, 'h3, 'h4} ");
      q.reverse;
      `checkp(q, "'{'h1, 'h3, 'h4, 'h2, 'h2} ");
      q.shuffle(); q.sort;
      `checkp(q, "'{'h1, 'h2, 'h2, 'h3, 'h4} ");

      // These require an with clause or are illegal
      // TODO add a lint check that with clause is provided
      qv = q.find with (item == 2);
      `checkp(qv, "'{'h2, 'h2} ");
      qv = q.find_first with (item == 2);
      `checkp(qv, "'{'h2} ");
      qv = q.find_last with (item == 2);
      `checkp(qv, "'{'h2} ");

      qv = q.find with (item == 20);
      `checkp(qv, "'{}");
      qv = q.find_first with (item == 20);
      `checkp(qv, "'{}");
      qv = q.find_last with (item == 20);
      `checkp(qv, "'{}");

      qi = q.find_index with (item == 2); qi.sort;
      `checkp(qi, "'{'h1, 'h2} ");
      qi = q.find_first_index with (item == 2);
      `checkp(qi, "'{'h1} ");
      qi = q.find_last_index with (item == 2);
      `checkp(qi, "'{'h2} ");

      qi = q.find_index with (item == 20); qi.sort;
      `checkp(qi, "'{}");
      qi = q.find_first_index with (item == 20);
      `checkp(qi, "'{}");
      qi = q.find_last_index with (item == 20);
      `checkp(qi, "'{}");

      qv = q.min;
      `checkp(qv, "'{'h1} ");
      qv = q.max;
      `checkp(qv, "'{'h4} ");

      // Reduction methods

      i = q.sum; `checkh(i, 32'hc);
      i = q.product; `checkh(i, 32'h30);

      q = '{32'b1100, 32'b1010, 32'b1100, 32'b1010, 32'b1010};
      i = q.and; `checkh(i, 32'b1000);
      i = q.or; `checkh(i, 32'b1110);
      i = q.xor; `checkh(i, 32'ha);

      $write("*-* All Finished *-*\n");
      $finish;
   end
endmodule
