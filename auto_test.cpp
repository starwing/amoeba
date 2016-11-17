#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
#include "catch.hpp"

#define AM_IMPLEMENTATION
#include "amoeba.h"

#define UNUSED(x) (void)(x)

#define STR_CANT_CREATE_SOLVER   "Unable to create solver!"
#define STR_CANT_CREATE_VARIABLE "Unable to create variable!"

static size_t g_nMemAllocated    = 0;
static size_t g_nMemAllocatedMax = 0;

static void *debug_allocf(void *ud, void *pCur, size_t nSizeNew, size_t nSizeCur) {
	UNUSED(ud);
	void *pNew = NULL;

	g_nMemAllocated += nSizeNew;
	g_nMemAllocated -= nSizeCur;
	if (g_nMemAllocatedMax < g_nMemAllocated) 
		g_nMemAllocatedMax = g_nMemAllocated;

	if (nSizeNew == 0) 
		free(pCur);
	else {
		pNew = realloc(pCur, nSizeNew);
		if(pNew == NULL)
			FAIL("No memory!");
	}
	return pNew;
}

int basic_memory_1() {
	am_Solver *pSolver = am_solver_new(debug_allocf, NULL);
	am_solver_del(pSolver);
	return g_nMemAllocated == 0;
}

int basic_memory_2() {
	am_Solver *pSolver = am_solver_new(debug_allocf, NULL);
	if (pSolver == NULL)
		FAIL(STR_CANT_CREATE_SOLVER);
	am_Variable *pVar = am_variable_new(pSolver);
	if (pSolver == NULL)
		FAIL(STR_CANT_CREATE_VARIABLE);
	am_solver_del(pSolver);
	return g_nMemAllocated == 0;
}

int basic_memory_3() {
	am_Solver *pSolver = am_solver_new(debug_allocf, NULL);
	if (pSolver == NULL)
		FAIL(STR_CANT_CREATE_SOLVER);
	am_Variable *pVar = am_variable_new(pSolver);
	if (pSolver == NULL)
		FAIL(STR_CANT_CREATE_VARIABLE);
	am_edit_add(pVar, AM_STRONG);
	am_edit_suggest(pVar, -10.0);
	BOOL bRes = am_approx(am_variable_get_value(pVar), -10.0);
	am_solver_del(pSolver);
	return  bRes && (g_nMemAllocated == 0);
}

TEST_CASE( "Factorials are computed", "[basic][memory]" ) {
	REQUIRE(basic_memory_1() == 1);
	REQUIRE(basic_memory_2() == 1);
	REQUIRE(basic_memory_3() == 1);
}
