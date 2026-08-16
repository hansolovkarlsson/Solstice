program TestClassArrayfieldChar;
type
    TWord = class
        letters: array[0..2] of char;
    end;
var
    w: TWord;
begin
    new(w);
    w.letters[0] := 'c';
    w.letters[1] := 'a';
    w.letters[2] := 't';

    writeln('letters: ', w.letters[0], w.letters[1], w.letters[2]);

    dispose(w);
end.
