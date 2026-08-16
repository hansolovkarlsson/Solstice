program TestProctypeFieldClass;

// A class field of a named procedural type - both write
// (c.handler := Double;) and call-through-read (c.handler(5)) needed
// real fixes, unlike the plain-record case: class field access goes
// through a completely different, heap-based mechanism
// (resolve_heap_deref_step()/build_heap_deref_write_statement()/
// parse_heap_deref_read()) that didn't know about procedural types at
// all before this. Double(5) = 10.

type
  TProc = function(x: integer): integer;
  TFoo = class
    name: integer;
    handler: TProc;
  end;

var f: TFoo;

function Double(x: integer): integer;
begin
  Double := x * 2;
end;

begin
  new(f);
  f.name := 1;
  f.handler := Double;
  writeln(f.handler(5));
  dispose(f);
end.
