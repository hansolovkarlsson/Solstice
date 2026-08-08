program TestClassSamenameMethods;
type
    TCircle = class
        radius: real;
        function Area: real;
    end;
    TSquare = class
        side: real;
        function Area: real;
    end;
var
    c: TCircle;
    s: TSquare;
begin
    { Two different classes each declaring a method called 'Area' -
      proves method headers are stored per-class (on each class's own
      PointerTypeDef entry), not in the single flat whole-program
      procedure namespace that would reject this as a duplicate. }
    writeln('declared ok');
end.
