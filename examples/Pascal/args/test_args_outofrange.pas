program TestArgsOutofrange;

// Run with: solvm test_args_outofrange.bin one
// ParamStr(ParamCount + 1) and ParamStr(-1) are both out of range -
// real Pascal/Free Pascal's own documented ParamStr behavior returns
// an EMPTY STRING for an out-of-range index, not a runtime error
// (unlike this VM's usual abort-on-out-of-range convention for array
// indexing) - both writeln calls below print a blank line, and the
// program still exits cleanly (0).

begin
  writeln(ParamStr(ParamCount + 1));
  writeln(ParamStr(-1));
  writeln('done');
end.
