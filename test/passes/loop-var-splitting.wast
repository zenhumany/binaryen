(module
  (func $work
    (local $x i32)
    (set_local $x (i32.const 0))
    (loop $loop
      (if (i32.const 1)
        (block
          (set_local $x (i32.const 2))
          (br $loop)
        )
      )
    )
  )
  (func $two
    (local $x i32)
    (set_local $x (i32.const 0))
    (loop $loop
      (if (i32.const 1)
        (block
          (set_local $x (i32.const 2))
          (br $loop)
        )
      )
      (if (i32.const 3)
        (block
          (set_local $x (i32.const 4))
          (br $loop)
        )
      )
    )
  )
  (func $ignore
    (local $x i32)
    (set_local $x (i32.const 0))
    (block $out
      (loop $loop
        (if (i32.const 1)
          (block
            (set_local $x (i32.const 2))
            (br $loop)
          )
        )
        (if (i32.const 3)
          (block
            (set_local $x (i32.const 4))
            (br $out)
          )
        )
      )
    )
  )
  (func $pair
    (local $x i32)
    (local $y i32)
    (set_local $x (i32.const 0))
    (set_local $y (i32.const -1))
    (loop $loop
      (if (i32.const 1)
        (block
          (set_local $x (i32.const 2))
          (set_local $y (i32.const -2))
          (br $loop)
        )
      )
    )
  )
  (func $pair-but-mix
    (local $x i32)
    (local $y i32)
    (set_local $x (i32.const 0))
    (set_local $y (i32.const -1))
    (loop $loop
      (if (i32.const 1)
        (block
          (set_local $x (i32.const 2))
          (set_local $y (get_local $x))
          (br $loop)
        )
      )
    )
  )
  (func $boring
    (loop
    )
    (loop $loop1
    )
  )
  (func $not-in-all
    (local $x i32)
    (set_local $x (i32.const 0))
    (loop $loop
      (if (i32.const 1)
        (block
          (set_local $x (i32.const 2))
          (br $loop)
        )
      )
      (if (i32.const 3)
        (block
          (br $loop) ;; backedge without $x as a final set
        )
      )
    )
  )
  (func $float
    (local $x f64)
    (set_local $x (f64.const 0))
    (loop $loop
      (if (i32.const 1)
        (block
          (set_local $x (f64.const 2))
          (br $loop)
        )
      )
    )
  )
  (func $return-blocks
    (local $x i32)
    (set_local $x (i32.const 0))
    (loop $loop
      (if (i32.const 1)
        (block
          (set_local $x (i32.const 2))
          (return)
          (br $loop)
        )
      )
    )
  )
  (func $unreachable-blocks
    (local $x i32)
    (set_local $x (i32.const 0))
    (loop $loop
      (if (i32.const 1)
        (block
          (set_local $x (i32.const 2))
          (unreachable)
          (br $loop)
        )
      )
    )
  )
  (func $if-blocks
    (local $x i32)
    (set_local $x (i32.const 0))
    (loop $loop
      (if (i32.const 1)
        (block
          (set_local $x (i32.const 2))
          (if (i32.const 3) (nop))
          (br $loop)
        )
      )
    )
  )
  (func $br-blocks
    (local $x i32)
    (set_local $x (i32.const 0))
    (block $out
      (loop $loop
        (if (i32.const 1)
          (block
            (set_local $x (i32.const 2))
            (br $out)
            (br $loop)
          )
        )
      )
    )
  )
  (func $other-allows
    (local $x i32)
    (set_local $x (i32.const 0))
    (loop $loop
      (if (i32.const 1)
        (block
          (set_local $x (i32.const 2))
          (call $other-allows)
          (nop)
          (drop (i32.eqz (i32.const 3)))
          (br $loop)
        )
      )
    )
  )
  (func $switch-ignorable
    (local $x i32)
    (set_local $x (i32.const 0))
    (block $out
      (loop $loop
        (if (i32.const 1)
          (block
            (set_local $x (i32.const 2))
            (br $loop)
          )
        )
        (br_table $out $out
          (i32.const 3)
        )
      )
    )
  )
  (func $switch-blocks1
    (local $x i32)
    (set_local $x (i32.const 0))
    (block $out
      (loop $loop
        (if (i32.const 1)
          (block
            (set_local $x (i32.const 2))
            (br $loop)
          )
        )
        (br_table $out $loop
          (i32.const 3)
        )
      )
    )
  )
  (func $switch-blocks2
    (local $x i32)
    (set_local $x (i32.const 0))
    (block $out
      (loop $loop
        (if (i32.const 1)
          (block
            (set_local $x (i32.const 2))
            (br $loop)
          )
        )
        (br_table $loop $out
          (i32.const 3)
        )
      )
    )
  )
  (func $br-if-blocks
    (local $x i32)
    (set_local $x (i32.const 0))
    (loop $loop
      (if (i32.const 1)
        (block
          (set_local $x (i32.const 2))
          (br $loop)
        )
      )
      (if (i32.const 3)
        (block
          (set_local $x (i32.const 4))
          (br_if $loop (i32.const 1))
        )
      )
    )
  )
)
