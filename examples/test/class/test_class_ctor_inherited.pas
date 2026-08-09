program TestClassCtorInherited;
type
    TAnimal = class
        legs: integer;
        procedure Init(l: integer);
    end;
    TDog = class(TAnimal)
        { never redeclares Init - purely inherited }
        breed: integer;
    end;
var
    d: TDog;

procedure TAnimal.Init;
begin
    legs := l;
end;

begin
    { d is TDog, Init is only declared on TAnimal - dynamic dispatch
      through the freshly-written tag must still find it. }
    new(d, Init(4));
    d.breed := 7;
    writeln('legs: ', d.legs);
    writeln('breed: ', d.breed);
    dispose(d);
end.
