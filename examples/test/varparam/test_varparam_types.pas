program TestVarParamTypes;
type
    TColor = (Red, Green, Blue);
    TRange = 1..10;
var
    r: real;
    b: boolean;
    c: char;
    s: string;
    col: TColor;
    rg: TRange;

procedure DoubleIt(var x: real);
begin
    x := x * 2.0;
end;

procedure Flip(var x: boolean);
begin
    x := not x;
end;

procedure NextChar(var x: char);
begin
    x := chr(ord(x) + 1);
end;

procedure Shout(var x: string);
begin
    x := uppercase(x);
end;

procedure NextColor(var x: TColor);
begin
    x := succ(x);
end;

{ a subrange var-parameter is bounds-checked on every write, using the
  PARAMETER's own declared bounds }
procedure SetRange(var x: TRange; val: integer);
begin
    x := val;
end;

begin
    r := 3.5;
    DoubleIt(r);
    writeln('r=', r);

    b := true;
    Flip(b);
    writeln('b=', b);

    c := 'a';
    NextChar(c);
    writeln('c=', c);

    s := 'hello';
    Shout(s);
    writeln('s=', s);

    col := Red;
    NextColor(col);
    writeln('col=', col);

    rg := 5;
    SetRange(rg, 8);
    writeln('rg=', rg);
end.
