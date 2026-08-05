program TestPtrLinkedlist;
{ The canonical self-referential linked-list pattern: PNode is declared
  BEFORE TNode, forward-referencing it (resolved once the whole 'type'
  section finishes parsing - see resolve_pending_pointer_types() in
  parser.c). Builds a 3-node list, prints it, then tears it down node by
  node. Expected output:
  10
  20
  30
  sum=60 }
type
    PNode = ^TNode;
    TNode = record
        data: integer;
        next: PNode;
    end;
var
    head, cur, tmp: PNode;
    i, sum: integer;
begin
    head := nil;
    for i := 3 downto 1 do begin
        new(tmp);
        tmp^.data := i * 10;
        tmp^.next := head;
        head := tmp;
    end;

    cur := head;
    while cur <> nil do begin
        writeln(cur^.data);
        cur := cur^.next;
    end;

    sum := 0;
    cur := head;
    while cur <> nil do begin
        sum := sum + cur^.data;
        tmp := cur;
        cur := cur^.next;
        dispose(tmp);
    end;
    writeln('sum=', sum);
end.
