program TestProctypeFieldAsArg;

// A class field's procedural value, read out, passed directly as an
// argument - both to an ordinary top-level function's own procedural
// parameter (Apply), and to a class METHOD's procedural parameter
// (f.ApplyHandler) - the latter needed its own fix in
// parse_class_method_call_arguments(), which previously assumed a
// method's own parameters were always plain scalars.
// Double(8) = 16, Double(9) = 18.

type
  TProc = function(x: integer): integer;
  TFoo = class
    handler: TProc;
    function ApplyHandler(fn: TProc; v: integer): integer;
  end;

var f: TFoo;

function Double(x: integer): integer;
begin
  Double := x * 2;
end;

function Apply(function fn(z: integer): integer; v: integer): integer;
begin
  Apply := fn(v);
end;

function TFoo.ApplyHandler;
begin
  ApplyHandler := fn(v);
end;

begin
  new(f);
  f.handler := Double;
  writeln(Apply(f.handler, 8));
  writeln(f.ApplyHandler(f.handler, 9));
  dispose(f);
end.
