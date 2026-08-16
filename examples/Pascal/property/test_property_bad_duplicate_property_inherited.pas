program PropBadDupPropInherited;
type
    TBase = class
    private
        FVal: integer;
    public
        property Val: integer read FVal;
    end;
    TChild = class(TBase)
    private
        FOther: integer;
    public
        { Redeclaring an INHERITED property's name is still a duplicate -
          properties can't be overridden in v1, see docs/LANGUAGE.md. }
        property Val: integer read FOther;
    end;
var
    c: TChild;
begin
    new(c);
    dispose(c);
end.
