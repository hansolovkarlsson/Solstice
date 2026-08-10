program TestProctypeFieldSelfshorthand;

// Reading/writing a procedural-typed class field via unqualified self.
// shorthand from inside a method body - a separate code path from an
// explicit c.field access, though it shares the same underlying fix
// (build_heap_deref_write_statement()/parse_heap_deref_read()) for the
// ordinary (non-array) field case. Double(6) = 12.

type
  TProc = function(x: integer): integer;
  TFoo = class
    handler: TProc;
    procedure SetHandler(p: TProc);
    function Run(x: integer): integer;
  end;

var f: TFoo;

function Double(x: integer): integer;
begin
  Double := x * 2;
end;

procedure TFoo.SetHandler;
begin
  handler := p;
end;

function TFoo.Run;
begin
  Run := handler(x);
end;

begin
  new(f);
  f.SetHandler(Double);
  writeln(f.Run(6));
  dispose(f);
end.
