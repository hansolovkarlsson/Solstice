program TestAbstractMixedConcrete;
type
    TShape = class
    private
        function SecretHelper: integer; abstract;
    public
        function Area: real; abstract;
        function TwiceHelper: integer;
    end;
    TCircle = class(TShape)
    private
        function SecretHelper: integer;
    public
        function Area: real;
    end;
var c: TCircle;

function TShape.TwiceHelper;
begin
    { a concrete method calling a PRIVATE abstract method by bare
      self-shorthand - works normally, only instantiation is gated by
      the abstract methods, not ordinary calls/visibility }
    TwiceHelper := SecretHelper * 2;
end;

function TCircle.SecretHelper;
begin
    SecretHelper := 21;
end;

function TCircle.Area;
begin
    Area := 5.0;
end;

begin
    new(c);
    writeln('Area = ', c.Area:0:2);
    writeln('TwiceHelper = ', c.TwiceHelper);
end.
