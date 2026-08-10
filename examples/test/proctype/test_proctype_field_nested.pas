program TestProctypeFieldNested;

// A procedural-typed field inside a NESTED RECORD field of a class -
// needed no extra fix at all, since a nested-record chain is already
// resolved down to a single combined offset by resolve_heap_deref_step()
// before the write/read machinery ever sees it, so the same fix that
// covers an ordinary top-level class field covers this for free.
// Double(5) = 10.

type
  TProc = function(x: integer): integer;
  TInner = record
    handler: TProc;
  end;
  TFoo = class
    inner: TInner;
  end;

var f: TFoo;

function Double(x: integer): integer;
begin
  Double := x * 2;
end;

begin
  new(f);
  f.inner.handler := Double;
  writeln(f.inner.handler(5));
  dispose(f);
end.
