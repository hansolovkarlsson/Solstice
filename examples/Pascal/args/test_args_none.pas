program TestArgsNone;

// Run with: solvm test_args_none.bin  (no extra arguments at all)
// ParamCount = 0, but ParamStr(0) is still the .bin path - argument 0
// always exists even when the program was given no arguments of its
// own.

begin
  writeln(ParamCount);
  writeln(ParamStr(0));
end.
