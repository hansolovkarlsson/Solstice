program TestProcParamBadType;

function Whoami(s: string): string;
begin
    Whoami := s;
end;

function Apply(function f(n: integer): integer; v: integer): integer;
begin
    Apply := f(v);
end;

begin
    writeln(Apply(Whoami, 5)); { wrong param type and return type }
end.
