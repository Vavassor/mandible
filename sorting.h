#pragma once

template <typename Type, typename LessThan>
void insertion_sort(Type* a, int size, const LessThan& compare) {
    for (int i = 1; i < size; ++i) {
        Type temp = a[i];
        int j;
        for(j = i; j > 0 && compare(temp, a[j - 1]); --j) {
            a[j] = a[j - 1];
        }
        a[j] = temp;
    }
}

template <typename Type, typename LessThan>
void quick_sort_innards(Type* a, int left, int right, const LessThan& compare) {
    while (left + 16 < right) {
        int middle = (left + right) / 2;
        Type median;
        if (compare(a[left], a[right])) {
            if (compare(a[middle], a[left])) {
                median = a[left];
            } else if (compare(a[middle], a[right])) {
                median = a[middle];
            } else {
                median = a[right];
            }
        } else {
            if (compare(a[middle], a[right])) {
                median = a[right];
            } else if (compare(a[middle], a[left])) {
                median = a[middle];
            } else {
                median = a[left];
            }
        }
        int i = left - 1;
        int j = right + 1;
        int pivot;
        for (;;) {
            while (compare(median, a[--j]));
            while (compare(a[++i], median));
            if (i >= j) {
                pivot = j;
                break;
            } else {
                Type temp = a[i];
                a[i] = a[j];
                a[j] = temp;
            }
        }
        quick_sort_innards(a, left, pivot, compare);
        left = pivot + 1;
    }
}

// Quick sort with median-of-three pivoting, followed by insertion sort for small partitions.
template <typename Type, typename LessThan>
void quick_sort(Type* a, int n, const LessThan& compare) {
    quick_sort_innards(a, 0, n - 1, compare);
    insertion_sort(a, n, compare);
}

template <typename Type, typename LessThanOrEqual>
void merge_sort(Type* a, Type* b, int begin, int end, const LessThanOrEqual& compare) {
    if (end - begin < 2) {
        return;
    }
    int middle = (end + begin) / 2;
    merge_sort(a, b, begin, middle, compare);
    merge_sort(a, b, middle, end, compare);
    int i = begin;
    int j = middle;
    for (int k = begin; k < end; ++k) {
        if (i < middle && (j >= end || compare(a[i], a[j]))) {
            b[k] = a[i++];
        } else {
            b[k] = a[j++];
        }
    }
    for (int k = begin; k < end; ++k) {
        a[k] = b[k];
    }
}
