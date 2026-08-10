program TestIsasSiblings;
type
    TAnimal = class
    public
        x: integer;
    end;
    TDog = class(TAnimal)
    public
        y: integer;
    end;
    TCat = class(TAnimal)
    public
        z: integer;
    end;
var
    pet: TAnimal;
    dog: TDog;
begin
    new(dog);
    { pet's STATIC type is TAnimal; 'pet is TCat' is a compile-time-valid
      check (TCat IS related to TAnimal, pet's declared type), even
      though pet's actual runtime tag will be TDog, not TCat }
    pet := dog;
    writeln('pet is TDog: ', pet is TDog);
    writeln('pet is TCat: ', pet is TCat);
    dispose(dog);
end.
