program TestSetEnumBool;
type
    TColor = (Red, Green, Blue, Yellow);
var
    colors: set of TColor;
    flags: set of boolean;
begin
    colors := [Red, Blue];
    writeln(Red in colors, ' ', Green in colors, ' ', Blue in colors); { TRUE FALSE TRUE }

    colors := colors + [Green];
    writeln(Green in colors); { TRUE }

    flags := [true];
    writeln(true in flags, ' ', false in flags); { TRUE FALSE }
end.
