(* This is a paren-star comment at the top of the file. *)
program TestParenStarComment;
var
    x, y: integer;
begin
    x := 5; (* set x *)
    y := (x + 1) * 2; (* mixing real parens/mul with a comment *)
    writeln(y); (* should print 12 *)
    (* a comment
       spanning multiple
       lines *)
    writeln(x);
end.
