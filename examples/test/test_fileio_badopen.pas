program TestFileioBadOpen;
var
    f: text;
begin
    assign(f, '/tmp/ouroboros_does_not_exist_xyz123.txt');
    reset(f);
end.
