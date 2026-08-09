program TestArgsBasic;

// Run with: solvm test_args_basic.bin hello world
// ParamCount = 2 (excludes ParamStr(0)). ParamStr(0) is the running
// .bin's own path (varies by where it's compiled/run from - just
// confirm it's non-empty and matches the .bin file actually invoked).
// ParamStr(1) = "hello", ParamStr(2) = "world".

begin
  writeln(ParamCount);
  writeln(ParamStr(0));
  writeln(ParamStr(1));
  writeln(ParamStr(2));
end.
