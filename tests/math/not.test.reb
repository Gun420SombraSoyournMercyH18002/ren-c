; functions/math/not.r
(false = not :abs)
(false = not #{})
(false = not charset "")
(false = not [])
(false = not #"a")
(false = not type-word!)
(false = not 1/1/2007)
(false = not 0.0)
(false = not me@mydomain.com)
(false = not %myfile)
(false = not func [] [])
(false = not first [:a])
(false = not 0)
(false = not #1444)
(false = not first ['a/b])
(false = not first ['a])
(false = not true)
(true = not false)
(false = not make map! [])
(false = not $0.00)
(false = not :append)
(false = not blank)
(false = not make object! [])
(false = not type of get '+)
(false = not 0x0)
(false = not first [()])
(false = not first [a/b])
(false = not make port! http://)
(false = not /refinement)
(false = not first [a/b:])
(false = not first [a:])
(false = not "")
(false = not <tag>)
(false = not 1:00)
(false = not 1.2.3)
(false = not http://)
(false = not 'a)
