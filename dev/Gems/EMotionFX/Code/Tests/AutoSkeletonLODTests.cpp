/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/

#include <Tests/SystemComponentFixture.h>
#include <EMotionFX/Source/Actor.h>
#include <EMotionFX/Source/ActorInstance.h>
#include <EMotionFX/Source/Node.h>
#include <EMotionFX/Source/Mesh.h>
#include <EMotionFX/Source/SubMesh.h>
#include <EMotionFX/Source/SkinningInfoVertexAttributeLayer.h>
#include <EMotionFX/Source/Skeleton.h>
#include <vector>
#include <string>

namespace EMotionFX
{
    struct AutoLODTestParams
    {
        std::vector<AZ::u32> m_skinningJointIndices; // The joint indices used while skinning.
        std::vector<std::string> m_criticalJoints; // The list of critical joints
        std::vector<AZ::u32> m_expectedEnabledJointIndices; // The expected enabled joints.
    };

    class AutoSkeletonLODFixture
        : public SystemComponentFixture
        , public ::testing::WithParamInterface<AutoLODTestParams>
    {
    public:
        void SetUp() override
        {
            SystemComponentFixture::SetUp();

            m_actor = nullptr;
            m_actorInstance = nullptr;
        }

        void TearDown() override
        {
            if (m_actorInstance)
            {
                m_actorInstance->Destroy();
                m_actorInstance = nullptr;
            }

            if (m_actor)
            {
                m_actor->Destroy();
                m_actor = nullptr;
            }

            SystemComponentFixture::TearDown();
        }

        SubMesh* SetupActor(AZ::u32 numSubMeshJoints)
        {
            // This creates an Actor with following hierarchy.
            // The numbers are the joint indices.
            //
            //                            5
            //                           /
            //                          /
            // 0-----1-----2-----3-----4
            //                          \
            //                           \
            //                            6
            //           
            // 7 (a node with skinned mesh)
            //
            // The mesh is on node 7, which is also a root node, just like joint number 0.
            // We (fake) skin the first six joints to the mesh of node 7.
            // Our test will actually skin to only a selection of these first seven joints.
            // We then test which joints get disabled and which not.
            m_actor = Actor::Create("testActor");

            // Create a chain of 5 nodes.
            Node* parent = nullptr;
            for (AZ::u32 i = 0; i < 5; ++i)
            {
                const AZStd::string jointName = AZStd::string::format("Joint%d", i);
                Node* newJoint = Node::Create(jointName.c_str(), m_actor->GetSkeleton());
                m_actor->AddNode(newJoint);

                newJoint->SetNodeIndex(i);
                if (parent)
                {
                    newJoint->SetParentIndex(parent->GetNodeIndex());
                    parent->AddChild(newJoint->GetNodeIndex());
                }

                parent = newJoint;
            }

            // Create two child nodes with the last node in the chain as parent.
            Node* childA = Node::Create("ChildA", m_actor->GetSkeleton());
            childA->SetNodeIndex(m_actor->GetNumNodes());
            childA->SetParentIndex(parent->GetNodeIndex());
            m_actor->AddNode(childA);
            parent->AddChild(childA->GetNodeIndex());

            Node* childB = Node::Create("ChildB", m_actor->GetSkeleton());
            childB->SetNodeIndex(m_actor->GetNumNodes());
            childB->SetParentIndex(parent->GetNodeIndex());
            m_actor->AddNode(childB);
            parent->AddChild(childB->GetNodeIndex());

            // Create a node that has a mesh.
            // Please note that we don't go the full way here, by also filling vertex position, normal and skinning data.
            // Every submesh stores a list of joints used to skin that submesh. We simply fill that list, as the auto-skeletal LOD algorithm looks at this list and doesn't 
            // look at the actual per vertex skinning information.
            // This way we simplify the test code slightly, while achieving the same correct test results.
            Node* meshNode = Node::Create("MeshNode", m_actor->GetSkeleton());
            meshNode->SetNodeIndex(m_actor->GetNumNodes());
            m_actor->AddNode(meshNode);
            Mesh* mesh = Mesh::Create();
            SubMesh* subMesh = SubMesh::Create(mesh, 0, 0, 0, 8, 24, 12, 0, numSubMeshJoints); // The numbers are just some dummy values for a fake mesh.
            mesh->AddSubMesh(subMesh);
            if (numSubMeshJoints > 0)
            {
                SkinningInfoVertexAttributeLayer* skinningLayer = SkinningInfoVertexAttributeLayer::Create(8); // Create a fake skinning layer.
                mesh->AddSharedVertexAttributeLayer(skinningLayer);
            }

            m_actor->SetMesh(0, meshNode->GetNodeIndex(), mesh);

            // Post init the actor.
            m_actor->ResizeTransformData();
            m_actor->PostCreateInit(/*makeGeomLodsCompatibleWithSkeletalLODs=*/false, /*generateOBBs=*/false, /*convertUnitType=*/false);

            return subMesh;
        }

    public:
        Actor* m_actor = nullptr;
        ActorInstance* m_actorInstance = nullptr;
    };

    TEST_F(AutoSkeletonLODFixture, VerifyHierarchy)
    {
        SetupActor(0);

        m_actorInstance = ActorInstance::Create(m_actor);

        ASSERT_NE(m_actor, nullptr);
        ASSERT_NE(m_actorInstance, nullptr);

        // Verify integrity of the hierarchy.
        ASSERT_EQ(m_actor->GetNumNodes(), 8);
        for (AZ::u32 i = 0; i < 4; ++i)
        {
            const Node* node = m_actor->GetSkeleton()->GetNode(i);
            const AZStd::string jointName = AZStd::string::format("Joint%d", i);
            ASSERT_EQ(node->GetNumChildNodes(), 1);
            ASSERT_STREQ(node->GetName(), jointName.c_str());
        }

        const Skeleton* skeleton = m_actor->GetSkeleton();
        ASSERT_EQ(skeleton->GetNode(4)->GetNumChildNodes(), 2);
        ASSERT_EQ(skeleton->GetNode(5)->GetParentNode(), skeleton->GetNode(4));
        ASSERT_EQ(skeleton->GetNode(5)->GetNumChildNodes(), 0);
        ASSERT_STREQ(skeleton->GetNode(5)->GetName(), "ChildA");
        ASSERT_EQ(skeleton->GetNode(6)->GetParentNode(), skeleton->GetNode(4));
        ASSERT_EQ(skeleton->GetNode(6)->GetNumChildNodes(), 0);
        ASSERT_STREQ(skeleton->GetNode(6)->GetName(), "ChildB");
        ASSERT_EQ(skeleton->GetNode(7)->GetNumChildNodes(), 0);
        ASSERT_STREQ(skeleton->GetNode(7)->GetName(), "MeshNode");
    }

    TEST_P(AutoSkeletonLODFixture, MainTest)
    {
        // Create a submesh that contains all joints, so act like we are skinned to all joints.
        const AZ::u32 numSkinJoints = static_cast<AZ::u32>(GetParam().m_skinningJointIndices.size());
        SubMesh* subMesh = SetupActor(numSkinJoints);
        for (AZ::u32 i = 0; i < numSkinJoints; ++i)
        {
            subMesh->SetBone(i, GetParam().m_skinningJointIndices[i]);
        }

        // Generate our skeletal LODs.
        AZStd::vector<AZStd::string> criticalJoints;
        for (const std::string& jointName : GetParam().m_criticalJoints)
        {
            criticalJoints.emplace_back(jointName.c_str());
        }
        m_actor->AutoSetupSkeletalLODsBasedOnSkinningData(criticalJoints);

        // Verify the skeletal LOD flags.
        m_actorInstance = ActorInstance::Create(m_actor);
        const Skeleton* skeleton = m_actor->GetSkeleton();

        // All nodes should be enabled as we use all joints.
        // And the mesh is also automatically enabled.
        for (AZ::u32 i = 0; i < 8; ++i) // 8 is the total number of joints plus one mesh node
        {
            const bool shouldBeEnabled = std::find(GetParam().m_expectedEnabledJointIndices.begin(), GetParam().m_expectedEnabledJointIndices.end(), i) != GetParam().m_expectedEnabledJointIndices.end();
            EXPECT_EQ(skeleton->GetNode(i)->GetSkeletalLODStatus(0), shouldBeEnabled);
        }

        // Make sure the actor instance's number of enabled joints is the same.
        EXPECT_EQ(m_actorInstance->GetNumEnabledNodes(), static_cast<AZ::u32>(GetParam().m_expectedEnabledJointIndices.size()));

        // Also make sure the enabled number of nodes contents reflects the expectation.
        for (AZ::u32 i = 0; i < m_actorInstance->GetNumEnabledNodes(); ++i)
        {
            const AZ::u32 enabledJointIndex = static_cast<AZ::u32>(m_actorInstance->GetEnabledNode(i));
            const bool enabledJointFound = std::find(GetParam().m_expectedEnabledJointIndices.begin(), GetParam().m_expectedEnabledJointIndices.end(), enabledJointIndex) != GetParam().m_expectedEnabledJointIndices.end();
            EXPECT_TRUE(enabledJointFound);
        }
    }

    // Our test parameters.
    const std::vector<AutoLODTestParams> testParams
    {
        {
            { 0, 1, 2, 3, 4, 5, 6 }, // All joints used in skinning. Number 7 excluded as that is the mesh.
            { }, // Critical joint list.
            { 0, 1, 2, 3, 4, 5, 6, 7 }  // We expect all nodes to be enabled.
        },
        {
            { }, // No skinning joints used, so just an actor with a static mesh.
            { }, // Critical joint list.
            { 0, 1, 2, 3, 4, 5, 6, 7 } // We expect everything to remain enabled.
        },
        {
            { 0, 1, 2, 3 }, // Skin only to the first 4 joints.
            { }, // Critical joint list.
            { 0, 1, 2, 3, 7 } // Expected enabled joints.
        },
        {
            { 0 }, // Skin only to the first joint.
            { }, // Critical joint list.
            { 0, 7 } // Expected enabled joints.
        },
        {
            { 0, 1 }, // Skin only to the first two joints.
            { }, // Critical joint list.
            { 0, 1, 7 } // Expected enabled joints.
        },
        {
            { 0, 6 }, // Skin only to the first and a leaf joint.
            { }, // Critical joint list.
            { 0, 1, 2, 3, 4, 6, 7 } // Expected enabled joints.
        },
        {
            { 4, 5 }, // Skin only to two joints down the hierarchy.
            { }, // Critical joint list.
            { 0, 1, 2, 3, 4, 5, 7 } // We expect all joints up to the root, and the mesh.
        },
        {
            { 6  }, // Skin to only one leaf joint.
            { }, // Critical joint list.
            { 0, 1, 2, 3, 4, 6, 7 } // We expect all joints up to the root, and the mesh.
        },
        {
            { 0, 1, 2  }, // Skin to only one leaf joint.
            { "ChildA" }, // Critical joint list.
            { 0, 1, 2, 3, 4, 5, 7 } // We expect all joints up to the root, and the mesh.
        },
        {
            { 0 }, // One joint.
            { "ChildA", "ChildB" }, // Critical joint list.
            { 0, 1, 2, 3, 4, 5, 6, 7 } // We expect all joints up to the root, and the mesh.
        },
        {
            { }, // No joints.
            { "ChildA", "ChildB" }, // Critical joint list.
            { 0, 1, 2, 3, 4, 5, 6, 7 } // We expect all joints up to the root, and the mesh.
        }
    };

    INSTANTIATE_TEST_CASE_P(AutoSkeletonLOD_Tests,
        AutoSkeletonLODFixture,
            ::testing::ValuesIn(testParams)
        );

} // namespace EMotionFX