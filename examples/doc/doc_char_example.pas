program DocCharExample;
var
    grade: char;
begin
    grade := 'A';
    writeln(grade);
    writeln('ordinal value: ', ord(grade));   { 65 }
    grade := chr(ord(grade) + 1);
    writeln('next letter: ', grade);           { B }
    writeln('newline via char code: ', 1, #10, 2); { 1, newline, 2 }
end.
