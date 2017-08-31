(module ;; sort by uses
  (func $a
    (call $a)
  )
  (func $b
    (call $b)
    (call $b)
  )
  (func $c
    (call $c)
    (call $c)
    (call $c)
  )
)
(module ;; sort by uses take 2
  (func $a
    (call $a)
  )
  (func $c
    (call $c)
    (call $c)
    (call $c)
  )
  (func $b
    (call $b)
    (call $b)
  )
)
(module ;; some identical uses, so similarity matters
  (func $a
    (call $a)
  )
  (func $b
    (call $b)
    (nop)
  )
  (func $c
    (call $c)
    (nop)
    (call $c)
  )
)
(module ;; some identical uses, so similarity matters take 2
  (func $a
    (call $a)
    (nop)
  )
  (func $b
    (call $b)
  )
  (func $c
    (call $c)
    (nop)
    (call $c)
  )
)
(module ;; with 4
  (func $a
    (call $a)
    (nop)
  )
  (func $b
    (call $b)
    (unreachable)
  )
  (func $c
    (call $c)
    (unreachable)
    (unreachable)
  )
  (func $d
    (call $d)
    (unreachable)
    (unreachable)
    (call $d)
  )
)
(module ;; with 4
  (func $d
    (call $d)
    (unreachable)
    (unreachable)
    (call $d)
  )
  (func $c
    (call $c)
    (unreachable)
    (unreachable)
  )
  (func $b
    (call $b)
    (unreachable)
  )
  (func $a
    (call $a)
    (nop)
  )
)
(module ;; with 4
  (func $a
    (call $a)
    (unreachable)
    (unreachable)
    (call $a)
  )
  (func $b
    (call $b)
    (unreachable)
    (unreachable)
  )
  (func $c
    (call $c)
    (unreachable)
  )
  (func $d
    (call $d)
    (nop)
  )
)

