program TestRecordBasic;
type
    TPerson = record
        age: integer;
        height: real;
        active: boolean;
        name: string;
        initial: char;
    end;
var
    p: TPerson;
begin
    p.age := 30;
    p.height := 1.75;
    p.active := true;
    p.name := 'Ada';
    p.initial := 'A';

    writeln('age: ', p.age);
    writeln('height: ', p.height);
    writeln('active: ', p.active);
    writeln('name: ', p.name);
    writeln('initial: ', p.initial);

    p.age := p.age + 1;
    writeln('age after increment: ', p.age);
end.
