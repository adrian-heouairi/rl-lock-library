#include "CUnit/Basic.h"
#include "rl_lock_library.c"

void test_seg_overlap() {
    CU_ASSERT(seg_overlap(0, 0, 4, 0)); // both to end of file
    CU_ASSERT_FALSE(seg_overlap(0, 10, 10, 20)); // consecutive segments
    CU_ASSERT(seg_overlap(15, 25, 17 , 22)); // z1 contains z2
    CU_ASSERT(seg_overlap(15, 25, 10, 16)); // z2 ends in z1
    CU_ASSERT_FATAL(seg_overlap(15, 0, 10, 15)); // z2 finishes right before z1
}

int main() {
    CU_pSuite pSuite = NULL;

    /* initialize the CUnit test registry */
    if (CUE_SUCCESS != CU_initialize_registry())
        return CU_get_error();

    /* add a suite to the registry */
    pSuite = CU_add_suite("Suite", NULL, NULL);
    if (NULL == pSuite) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    /* add the tests to the suite */
    if (NULL == CU_add_test(pSuite, "test of seg_overlap", test_seg_overlap)) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    /* Run all tests using the CUnit Basic interface */
    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    CU_cleanup_registry();
    return CU_get_error();    
}