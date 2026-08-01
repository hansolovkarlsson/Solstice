program TestReadMultitarget;
type
    TPoint = record
        x, y: integer;
    end;
var
    g: integer;
    p: TPoint;
    a, b: integer;

procedure Foo(param: integer);
var
    static s: integer;
begin
    { a single readln/read call can target a mix of kinds - global,
      parameter, static local, and a record field - each resolved
      exactly as it would be on its own }
    readln(g, param, s, p.x);
    writeln('g=', g, ' param=', param, ' s=', s, ' p.x=', p.x);
end;

begin
    Foo(0);

    { readln(a, b) must flush only after the LAST target, so a second
      readln(...) on the next line still starts cleanly }
    readln(a, b);
    readln(g);
    writeln('a=', a, ' b=', b, ' g=', g);
end.
