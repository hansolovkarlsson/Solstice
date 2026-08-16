program TestSealedWithParent;
type
    TAnimal = class
        name: integer;
        function Speak: integer;
    end;
    TDog = class sealed(TAnimal)
        function Speak: integer;
    end;
var
    d: TDog;

function TAnimal.Speak;
begin
    Speak := 0;
end;

function TDog.Speak;
begin
    Speak := 1;
end;

begin
    new(d);
    writeln(d.Speak);
    dispose(d);
end.
