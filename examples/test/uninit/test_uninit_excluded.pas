program TestUninitExcluded;
{ None of these should produce an uninitialized-variable warning: a
  'var' parameter (always a valid reference), a 'static' local (reads
  its implicit zero on the first call - a common, intentional pattern),
  an array local (this pass doesn't track per-element initialization),
  and a local passed by reference to another procedure that sets it. }
var
    g: integer;

procedure SetIt(var v: integer);
begin
    v := 42;
end;

procedure P(var vp: integer);
var
    static s: integer;
    arr: array[1..3] of integer;
    i, x: integer;
begin
    vp := vp + 1;
    writeln(s);
    for i := 1 to 3 do
        arr[i] := i;
    writeln(arr[1]);
    SetIt(x);
    writeln(x);
end;

begin
    g := 10;
    P(g);
end.
