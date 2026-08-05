program TestPtrLocal;
{ A pointer as a local variable, a by-value parameter, and a 'var'
  parameter (re-pointed inside the procedure, visible to the caller
  afterward - a pointer variable itself needs no new by-reference
  mechanism, since its value is just an int like any other scalar 'var'
  parameter). Expected output:
  local=7
  byvalue unchanged=1
  varparam changed=99 }
type
    PInt = ^integer;
var
    a: PInt;

procedure MakeLocal;
var
    p: PInt;
begin
    new(p);
    p^ := 7;
    writeln('local=', p^);
    dispose(p);
end;

procedure ByValue(p: PInt);
begin
    new(p); { rebinding the local copy of p must not affect the caller's own p }
    p^ := 999;
    dispose(p);
end;

procedure ByVar(var p: PInt);
begin
    new(p);
    p^ := 99;
end;

begin
    MakeLocal;

    new(a);
    a^ := 1;
    ByValue(a);
    writeln('byvalue unchanged=', a^);
    dispose(a);

    a := nil;
    ByVar(a);
    writeln('varparam changed=', a^);
    dispose(a);
end.
