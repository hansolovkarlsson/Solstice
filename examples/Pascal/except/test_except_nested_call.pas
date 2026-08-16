program TestExceptNestedCall;

// A raise from several call frames deep inside the try-body, caught by
// the OUTER try - proves the call-stack/frame-stack/operand-stack
// unwind (sp/call_sp/fp/frame_sp all restored to their try-entry
// snapshot) is correct, not just a same-frame jump. Each intervening
// procedure has its own locals, so a naive same-frame-only jump would
// leave the frame stack corrupted - the trailing arithmetic after the
// catch proves it isn't.

var result: integer;

procedure Deep;
var x, y: integer;
begin
  x := 10;
  y := 20;
  raise 'deep error';
  writeln(x + y); // unreachable
end;

procedure Middle;
var z: integer;
begin
  z := 5;
  Deep;
  writeln(z); // unreachable
end;

begin
  result := 0;
  try
    Middle;
    result := 1; // unreachable
  except
    writeln('caught: ', ExceptMessage);
    result := 99;
  end;
  writeln('result = ', result);
  result := result + 1;
  writeln('result = ', result);
end.
