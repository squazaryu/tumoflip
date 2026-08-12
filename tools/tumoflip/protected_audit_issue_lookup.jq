.[]
| select(
    .title == $title
    and (
      (.body | contains($marker))
      or (
        (.body | contains($source))
        and (.body | contains($base))
        and (.body | contains($extra))
      )
    )
  )
| .number
