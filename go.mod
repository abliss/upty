module github.com/abliss/upty

go 1.14

require gvisor.dev/gvisor v0.0.0
replace (
	gvisor.dev/gvisor => ./gvisor
)