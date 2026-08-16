program TestProcParamProcedure;

var
    total: integer;

procedure AddOne(n: integer);
begin
    total := total + n + 1;
end;

procedure AddTen(n: integer);
begin
    total := total + n + 10;
end;

procedure RunTwice(procedure p(n: integer); v: integer);
begin
    p(v);
    p(v);
end;

begin
    total := 0;
    RunTwice(AddOne, 5);
    writeln(total); { 0 + 6 + 6 = 12 }
    total := 0;
    RunTwice(AddTen, 5);
    writeln(total); { 0 + 15 + 15 = 30 }
end.
