(module
  (import $get "env" "get" (result i32))
  (import $set "env" "set" (param i32))
  (func "export" $split-loop-vars
    (local $x i32)
    (local $y i32)
    (set_local $x (i32.const 0))
    (loop $loop
      (call_import $set (get_local $x))
      (set_local $y (i32.add (get_local $x) (i32.const 1)))
      (call_import $set (get_local $y))
      (call_import $set (get_local $x))
      (if (call_import $get)
        (block
          (set_local $x (get_local $y))
          (br $loop)
        )
      )
    )
  )
)
