#   Copyright (c) 2018 PaddlePaddle Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import print_function

import unittest
import numpy as np
from op_test import OpTest

import paddle.fluid.core as core
from paddle.fluid.op import Operator
import paddle.fluid as fluid
from paddle.fluid import compiler, Program, program_guard


# Situation 1: Attr(shape) is a list(without tensor)
class TestFillConstantOp1(OpTest):
    def setUp(self):
        '''Test fill_constant op with specified value
        '''
        self.op_type = "fill_constant"

        self.inputs = {}
        self.attrs = {'shape': [123, 92], 'value': 3.8}
        self.outputs = {'Out': np.full((123, 92), 3.8)}

    def test_check_output(self):
        self.check_output()


class TestFillConstantOp2(OpTest):
    def setUp(self):
        '''Test fill_constant op with default value
        '''
        self.op_type = "fill_constant"

        self.inputs = {}
        self.attrs = {'shape': [123, 92]}
        self.outputs = {'Out': np.full((123, 92), 0.0)}

    def test_check_output(self):
        self.check_output()


class TestFillConstantOp3(OpTest):
    def setUp(self):
        '''Test fill_constant op with specified int64 value
        '''
        self.op_type = "fill_constant"

        self.inputs = {}
        self.attrs = {'shape': [123, 92], 'value': 10000000000}
        self.outputs = {'Out': np.full((123, 92), 10000000000)}

    def test_check_output(self):
        self.check_output()


class TestFillConstantOp4(OpTest):
    def setUp(self):
        '''Test fill_constant op with specified int value
        '''
        self.op_type = "fill_constant"

        self.inputs = {}
        self.attrs = {'shape': [123, 92], 'value': 3}
        self.outputs = {'Out': np.full((123, 92), 3)}

    def test_check_output(self):
        self.check_output()


class TestFillConstantOpWithSelectedRows(OpTest):
    def check_with_place(self, place):
        scope = core.Scope()
        # create Out Variable
        out = scope.var('Out').get_selected_rows()

        # create and run fill_constant_op operator
        fill_constant_op = Operator(
            "fill_constant", shape=[123, 92], value=3.8, Out='Out')
        fill_constant_op.run(scope, place)

        # get result from Out
        result_array = np.array(out.get_tensor())
        full_array = np.full((123, 92), 3.8, 'float32')

        self.assertTrue(np.array_equal(result_array, full_array))

    def test_fill_constant_with_selected_rows(self):
        places = [core.CPUPlace()]
        if core.is_compiled_with_cuda():
            places.append(core.CUDAPlace(0))

        for place in places:
            self.check_with_place(place)


# Situation 2: Attr(shape) is a list(with tensor)
class TestFillConstantOp1_ShapeTensorList(OpTest):
    def setUp(self):
        '''Test fill_constant op with specified value
        '''
        self.op_type = "fill_constant"
        self.init_data()
        shape_tensor_list = []
        for index, ele in enumerate(self.shape):
            shape_tensor_list.append(("x" + str(index), np.ones(
                (1)).astype('int32') * ele))

        self.inputs = {"ShapeTensorList": shape_tensor_list}
        self.attrs = {'shape': self.infer_shape, 'value': self.value}
        self.outputs = {'Out': np.full(self.shape, self.value)}

    def init_data(self):
        self.shape = [123, 92]
        self.infer_shape = [-1, 92]
        self.value = 3.8

    def test_check_output(self):
        self.check_output()


class TestFillConstantOp2_ShapeTensorList(OpTest):
    def setUp(self):
        '''Test fill_constant op with default value
        '''
        self.op_type = "fill_constant"
        self.init_data()
        shape_tensor_list = []
        for index, ele in enumerate(self.shape):
            shape_tensor_list.append(("x" + str(index), np.ones(
                (1)).astype('int32') * ele))

        self.inputs = {"ShapeTensorList": shape_tensor_list}
        self.attrs = {'shape': self.infer_shape}
        self.outputs = {'Out': np.full(self.shape, 0.0)}

    def init_data(self):
        self.shape = [123, 92]
        self.infer_shape = [-1, -1]

    def test_check_output(self):
        self.check_output()


class TestFillConstantOp3_ShapeTensorList(TestFillConstantOp1_ShapeTensorList):
    def init_data(self):
        self.shape = [123, 92]
        self.infer_shape = [123, -1]
        self.value = 10000000000


class TestFillConstantOp4_ShapeTensorList(TestFillConstantOp1_ShapeTensorList):
    def init_data(self):
        self.shape = [123, 92]
        self.infer_shape = [123, -1]
        self.value = 3


# Situation 3: shape is a tensor
class TestFillConstantOp1_ShapeTensor(OpTest):
    def setUp(self):
        '''Test fill_constant op with specified value
        '''
        self.op_type = "fill_constant"
        self.init_data()

        self.inputs = {"ShapeTensor": np.array(self.shape).astype("int32")}
        self.attrs = {'value': self.value}
        self.outputs = {'Out': np.full(self.shape, self.value)}

    def init_data(self):
        self.shape = [123, 92]
        self.value = 3.8

    def test_check_output(self):
        self.check_output()


# # Test python API
class TestFillConstantAPI(OpTest):
    def test_api(self):
        positive_2 = fluid.layers.fill_constant([1], "int32", 2)
        shape_tensor = fluid.layers.data(
            name="shape_tensor",
            shape=[2],
            append_batch_size=False,
            dtype="int32")

        out_1 = fluid.layers.fill_constant(
            shape=[1, 2], dtype="float32", value=1.1)
        out_2 = fluid.layers.fill_constant(
            shape=[1, positive_2], dtype="float32", value=1.1)

        out_3 = fluid.layers.fill_constant(
            shape=shape_tensor, dtype="float32", value=1.1)

        exe = fluid.Executor(place=fluid.CPUPlace())
        res_1, res_2, res_3 = exe.run(
            fluid.default_main_program(),
            feed={"shape_tensor": np.array([1, 2]).astype("int32")},
            fetch_list=[out_1, out_2, out_3])

        assert np.array_equal(res_1, np.full([1, 2], 1.1, dtype="float32"))
        assert np.array_equal(res_2, np.full([1, 2], 1.1, dtype="float32"))
        assert np.array_equal(res_3, np.full([1, 2], 1.1, dtype="float32"))


class TestFillConstantOpError(OpTest):
    def test_errors(self):
        with program_guard(Program(), Program()):
            #for ci coverage
            x1 = fluid.layers.data(name='x1', shape=[1], dtype="int16")
            self.assertRaises(
                ValueError,
                fluid.layers.fill_constant,
                shape=[1],
                value=5,
                dtype='uint4')
            self.assertRaises(
                TypeError,
                fluid.layers.fill_constant,
                shape=[1],
                value=5,
                dtype='int16',
                out=x1)
            # The input dtype of fill_constant must be one of bool, float16,
            #float32, float64, int32 or int64
            x2 = fluid.layers.data(name='x2', shape=[1], dtype="int32")

            self.assertRaises(
                TypeError,
                fluid.layers.fill_constant,
                shape=[1],
                value=5,
                dtype='uint8')
            self.assertRaises(
                TypeError,
                fluid.layers.fill_constant,
                shape=[1],
                value=5,
                dtype='float64',
                out=x2)

            # test Error of Shape
            def test_shape_type():
                fluid.layers.fill_constant(shape=1, dtype="float32", value=1)

            self.assertRaises(TypeError, test_shape_type)

            def test_shape_size():
                fluid.layers.fill_constant(shape=[], dtype="float32", value=1)

            self.assertRaises(AssertionError, test_shape_size)


if __name__ == "__main__":
    unittest.main()
