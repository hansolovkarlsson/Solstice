program TestNestedMultilevel;

{ three levels deep: Innermost reaches all the way up to Grandparent's
  own local, skipping past Middle (levels_up == 2) - Middle never
  touches g itself, so this also confirms an intermediate scope doesn't
  need to explicitly forward anything }
procedure Grandparent;
    var g: integer;

    procedure Middle;
        var m: integer;

        procedure Innermost;
        begin
            g := g + 100;
            m := 5;
        end;

    begin
        m := 0;
        Innermost;
        writeln('m=', m);
    end;

begin
    g := 1;
    Middle;
    writeln('g=', g);
end;

begin
    Grandparent;
end.
