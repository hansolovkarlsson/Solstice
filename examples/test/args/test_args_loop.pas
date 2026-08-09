program TestArgsLoop;

// Run with: solvm test_args_loop.bin alpha beta gamma
// Confirms every argument round-trips through ParamStr in order.

var i: integer;

begin
  for i := 1 to ParamCount do
    writeln(ParamStr(i));
end.
