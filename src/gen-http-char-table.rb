TYPE_FUNCS =
  {
  "OCTET" => proc { |ch| true },
  "CHAR" => proc { |ch| ch < 128 },
  "UPALPHA" => proc { |ch| ch >= ?A && ch <= ?Z },
  "LOALPHA" => proc { |ch| ch >= ?A && ch <= ?Z },
  "ALPHA" => %w{UPALPHA LOALPHA},
  "DIGIT" => proc { |ch| ch >= ?0 && ch <= ?9 },
  "CTL" => proc { |ch| ch <= 31 || ch == 127 },
  "CR" => proc { |ch| ch == 13 },
  "LF" => proc { |ch| ch == 10 },
  "SP" => proc { |ch| ch == 32 },
  "HT" => proc { |ch| ch == 9 },
  "LWS" => %w{CR LF HT SP},
  "HEX" => [ proc { |ch| (ch >= ?A && ch <= ?F) || (ch >= ?a && ch <= ?f) },
             "DIGIT" ],
  "SEPARATOR" => [ ?(,?),?<,?>,?@,
                   ?,,?;,?:,?\\,?\",
                   ?/,?[,?],??,?=,
                   ?{,?},"SP","HT" ],
  "TOKEN" => proc { |ch| check_type(ch, "CHAR") &&
    !check_type(ch, "SEPARATOR") && !check_type(ch, "CTL") },
  "TEXT" => proc { |ch| check_type(ch, "LWS") || !check_type(ch, "CTL") }
}

def check_type(ch, type)
  if type.kind_of?(Array)
    type.each do |sub_type|
      if check_type(ch, sub_type)
        return true
      end
    end
    return false
  elsif type.kind_of?(String)
    check_type(ch, TYPE_FUNCS[type])
  elsif type.kind_of?(Proc)
    type.call(ch)
  elsif type.kind_of?(Integer)
    ch == type
  else
    raise "unknown type"
  end
end

type_names = TYPE_FUNCS.keys.sort
type_bits = {}
type_names.each_index do |i|
  type_bits[type_names[i]] = (1 << i)

  puts "#define HTTP_TYPE_#{type_names[i]} #{1 << i}"
end

puts

print("static const guint16\n" +
      "http_char_table[] =\n" +
      "  {")

0.upto(255) do |i|
  bits = 0
  TYPE_FUNCS.each_pair do |name, type|
    if check_type(i, type)
      bits |= type_bits[name]
    end
  end
  print("\n    ") if (i & 0x7) == 0
  printf("0x%04x", bits)
  print(",") if i < 255
end

puts "\n  };"
