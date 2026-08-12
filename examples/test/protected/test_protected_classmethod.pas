program TestProtectedClassmethod;
type
    TBase = class
    protected
        class function Helper: integer;
    end;
    TSub = class(TBase)
        function UseHelper: integer;
    end;
var
    s: TSub;

function TBase.Helper;
begin
    Helper := 99;
end;

function TSub.UseHelper;
begin
    UseHelper := TSub.Helper;   { protected class method, called from a descendant - allowed }
end;

begin
    new(s);
    writeln(s.UseHelper);
    dispose(s);
end.
