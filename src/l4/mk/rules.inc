# vi:ft=make
# This makefile contains the generator for our basic pattern rules.
# These generators allow easy instantiation of own rule sets.

# variables for instantiating make rules.
# for C++ compile (CXX) use $(CXX), $(INT_CXX_NAME), and $(CXXFLAGS)
BID_CXX_COMPILER = CXX
BID_CXX_COMPILER_INT_NAME = INT_CXX_NAME
# for C compile (C) use $(CC), $(INT_CPP_NAME), and $(CFLAGS)
BID_C_COMPILER = CC
BID_C_COMPILER_INT_NAME = INT_CPP_NAME
# for AS compile (AS) use $(CC), $(INT_CPP_NAME), and $(ASFLAGS)
BID_AS_COMPILER = CC
BID_AS_COMPILER_INT_NAME = INT_CPP_NAME

# Template for our make rules.
# arg1: the target or target pattern (e.g., %.o)
# arg2: the prerequisites (e.g., %.c)
# arg3: the compiler pattern, used for determining the compiler
#       variables and default flags (e.g., CXX). The rule uses
#       $($(BID_<arg3>_COMPILER)) as compiler,
#       $($(BID_<arg3>_COMPILER_INT_NAME)) for gendep, and
#       $(<arg3>FLAGS) as flags
# arg4: extra command line flags for the compile
define BID_MAKE_RULE_template_gendep
$(1): $(2)
	@$$(COMP_MESSAGE)
	$$(VERBOSE)$$(call MAKEDEP,$$($(BID_$(3)_COMPILER_INT_NAME))) $$($(BID_$(3)_COMPILER)) -c $$(DEPEND_FLAG) $$(CPPFLAGS) $$($(3)FLAGS) $(4) $$(call absfilename,$$<) -o $$@
	$$(DEPEND_VERBOSE)$$(call DEPEND_EXTEND_FUNC, $$(*F).d, $$(dir $$@).$$(notdir $$@).d)
endef

# Template for our make rules (make rules that do not use gendep)
define BID_MAKE_RULE_template_nongendep
$(1): $(2)
	@$$(COMP_MESSAGE)
	$$(VERBOSE)$$($(BID_$(3)_COMPILER)) -c $$(DEPFLAGS) $$(CPPFLAGS) $$($(3)FLAGS) $(4) $$(call absfilename,$$<) -o $$@
endef

# which template rule do we really use
BID_MAKE_RULE_template = $(BID_MAKE_RULE_template_gendep)

#
# Generate the default set of make rules.
# targets: .o, .s.o, .pr.o, and .pr.s.o
#
define BID_GENERATE_DEFAULT_MAKE_RULES
  $(call BID_MAKE_RULE_template,$(1).o,$(2),$(3),$(4))
  $(call BID_MAKE_RULE_template,$(1).s.o,$(2),$(3),$(4) $$(PICFLAGS))
  $(call BID_MAKE_RULE_template,$(1).pr.o,$(2),$(3),$(4) -DPROFILE -pg)
  $(call BID_MAKE_RULE_template,$(1).pr.s.o,$(2),$(3),$(4) $$(PICFLAGS) -DPROFILE -pg)
endef

#
# Generate the C++ set of make rules.
# prerequisites: %.<arg1>  (usually %.cc)
# targets: %.o, %.s.o, %.ne.o, %.s.ne.o, %.pr.o, and %.pr.s.o
#
define BID_GENERATE_CXX_MAKE_RULES
  $(call BID_GENERATE_DEFAULT_MAKE_RULES,%,%.$(1),CXX)
  $(call BID_MAKE_RULE_template,%.ne.o,%.$(1),CXX,$$(CXXFLAGS_NOEXC))
  $(call BID_MAKE_RULE_template,%.s.ne.o,%.$(1),CXX,$$(CXXFLAGS_NOEXC) $$(PICFLAGS))
endef

#
#
# Generate the C set of make rules.
# prerequisites: %.<arg1> (usually %.c)
# targets: %.o, %.s.o, %.pr.o, and %.pr.s.o
#
define BID_GENERATE_C_MAKE_RULES
  $(call BID_GENERATE_DEFAULT_MAKE_RULES,%,%.$(1),C)
endef


