program TestVarParamBasic;
var
    g: integer;

procedure Inc10(var x: integer);
begin
    x := x + 10;
end;

procedure Swap(var a, b: integer);
var
    temp: integer;
begin
    temp := a;
    a := b;
    b := temp;
end;

{ forwarding: Outer's own 'var' parameter is passed straight through to
  Inner - both must end up mutating whichever variable the ORIGINAL
  caller passed in. }
procedure Inner(var x: integer);
begin
    x := x * 2;
end;

procedure Outer(var y: integer);
begin
    Inner(y);
end;

procedure SwapLocals;
var
    x, y: integer;
begin
    x := 1;
    y := 2;
    Swap(x, y);
    writeln('x=', x, ' y=', y);
end;

begin
    g := 5;
    Inc10(g);
    writeln('g=', g);

    SwapLocals;

    g := 7;
    Outer(g);
    writeln('g=', g);
end.
